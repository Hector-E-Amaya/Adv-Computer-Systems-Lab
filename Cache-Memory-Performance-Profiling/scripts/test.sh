#!/usr/bin/env bash
# Correctness and sanity tests for the memory benchmarks.
#   1. topology: one logical CPU per physical core, siblings paired
#   2. lat: the pointer chain is ONE cycle over every node (random/seq/page)
#   3. mutation: a broken cycle builder (lat_mutant) must be caught
#   4. bw: every pattern covers every line once; reads sum correctly;
#      writes land exactly; untouched bytes unchanged (all mixes/granularities)
#   5. kern: results match a double-precision reference
#   6. asm: chase loops are dependent-load chains without stack spills (K <= 12)
#   7. sanity vs known values: L1 latency ~5 cycles, DRAM latency plausible,
#      MLP raises throughput, bandwidth under the 25.6 GB/s channel peak,
#      unloaded `loaded` agrees with `lat`
# Log: data/raw/correctness.log. Nonzero exit if anything fails.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=data/raw/correctness.log
pass=0
fail=0
{
    echo "# correctness run $(date -u +%Y-%m-%dT%H:%M:%SZ), commit $(git rev-parse --short HEAD)"
} > "$LOG"

ok() { pass=$((pass + 1)); echo "PASS $*" >> "$LOG"; }
bad() { fail=$((fail + 1)); echo "FAIL $*" | tee -a "$LOG"; }

# expect_exit CODE cmd...: records the command's own PASS/FAIL line
expect_exit() {
    local want=$1 out
    shift
    out=$("$@" 2>&1)
    local got=$?
    echo "$out" >> "$LOG"
    if [ "$got" -eq "$want" ]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAILED (exit $got, want $want): $*"; fi
}

# in_range NAME VALUE LO HI
in_range() {
    if python -c "import sys; v,lo,hi=map(float,sys.argv[1:]); sys.exit(0 if lo<=v<=hi else 1)" "$2" "$3" "$4"; then
        ok "$1 = $2 in [$3, $4]"
    else
        bad "$1 = $2 not in [$3, $4]"
    fi
}

for t in lat bw loaded kern lat_mutant lat_slotmutant; do
    [ -x build/$t.exe ] || { echo "missing build/$t.exe (run scripts/build.sh)"; exit 1; }
done

echo "[1] topology"
topo=$(build/lat.exe --topology | tr -d '\r')
echo "$topo" >> "$LOG"
ncores=$(echo "$topo" | grep -c '^core')
paired=$(echo "$topo" | awk '{n=NF-4} n==2 && $(NF)==$(NF-1)+1' | wc -l) # "core k: logical cpus a b"
[ "$ncores" -ge 2 ] && [ "$paired" -eq "$ncores" ] && ok "topology: $ncores cores, SMT siblings adjacent" \
    || bad "topology unexpected ($ncores cores, $paired paired)"

echo "[2] lat single-cycle checks"
for s in 128 4K 64K 1M 16M 256M; do expect_exit 0 build/lat.exe --size $s --check; done
for s in 4K 1M; do expect_exit 0 build/lat.exe --size $s --mode seq --check; done
for s in 8K 64K 1M 8M 32M; do expect_exit 0 build/lat.exe --size $s --mode page --check; done

echo "[3] mutation: broken cycle builder must FAIL the check"
for s in 64K 1M 16M; do expect_exit 1 build/lat_mutant.exe --size $s --check; done
echo "[3b] mutation: the old page layout (64 of 1024 L2 sets) must FAIL set-balance"
for s in 8M 32M; do expect_exit 1 build/lat_slotmutant.exe --size $s --mode page --check; done

echo "[4] bw coverage / checksum / write checks"
for p in "seq" "stride --stride 64" "stride --stride 256" "stride --stride 1024" "random"; do
    for w in 0 30 50 100; do
        for gr in 8 16 32 64; do
            for nt in "" "--nt"; do
                for s in 256K 192K; do
                    expect_exit 0 build/bw.exe --size $s --pattern $p --wpct $w --gran $gr $nt --check
                done
            done
        done
    done
done

echo "[5] kern reference checks"
for k in saxpy mul; do for n in 1 7 1000 1000003; do expect_exit 0 build/kern.exe --kernel $k --n $n --check; done; done

echo "[5b] regressions for bugs found in the pre-sweep code review"
# B8: partial-line non-temporal measurements are refused
expect_exit 2 build/bw.exe --size 1M --nt --gran 8 --trials 1
# B3: --smt places one thread per physical core before any sibling
cpus2=$(build/bw.exe --size 1M --threads 2 --smt --dur 0.02 --trials 1 | awk -F, '{print $NF}')
[ "$cpus2" = "2 4" ] && ok "bw --smt 2 threads on separate cores ($cpus2)" || bad "bw --smt 2 threads cpus = '$cpus2', want '2 4'"
cpus5=$(build/bw.exe --size 1M --threads 5 --smt --dur 0.02 --trials 1 | awk -F, '{print $NF}')
[ "$cpus5" = "2 4 6 0 3" ] && ok "bw --smt 5 threads: cores first, then a sibling ($cpus5)" || bad "bw --smt 5 threads cpus = '$cpus5'"
# B2: line rate and useful GB/s come from the same trial
row=$(build/bw.exe --size 1M --gran 32 --dur 0.02 --trials 5)
in_range "bw mlines/s x gran / useful GB/s (same trial)" \
    "$(echo "$row" | awk -F, '{print $14 * $4 / 1000 / $11}')" 0.999 1.001
# B1: worker start is not delayed by the main thread spinning on worker 0's CPU
wall=$(python -c "import os, subprocess, time
t = time.perf_counter()
subprocess.run([os.path.abspath('build/bw.exe'), '--size', '1M', '--threads', '4', '--dur', '0.1', '--trials', '5'], capture_output=True)
print(time.perf_counter() - t)")
in_range "bw 4 threads, 6 x 0.1 s trials: wall seconds" "$wall" 0.5 3
# B4: both kernels vectorized (256-bit)
for f in saxpy mul; do
    # gcc may emit a specialised copy (e.g. saxpy.constprop.0): match any suffix
    n=$(awk -v f="$f" '$0 ~ "^"f"[.a-z0-9]*:$" {on=1} on' build/asm/kern.s | sed '/seh_endproc/q' |
        grep -cE 'v(fmadd[0-9]+|mul)ps.*ymm')
    [ "$n" -gt 0 ] && ok "asm: kern $f uses ymm packed math" || bad "asm: kern $f not vectorized"
done
# loaded --smt: traffic never shares the chase core (CPU 2/3) until all else is used
lcpus=$(build/loaded.exe --threads 6 --smt --delay 0 --trials 1 --window 0.01 --chase-size 1M --traffic-size 1M | awk -F, '{print $NF}')
[ "$lcpus" = "4 6 0 5 7 1" ] && ok "loaded --smt 6 traffic threads avoid the chase core ($lcpus)" \
    || bad "loaded --smt cpus = '$lcpus', want '4 6 0 5 7 1'"
# loaded DRAM estimate: read-only traffic -> equals total; 100% writes -> ~2x traffic + chase
r0=$(build/loaded.exe --threads 1 --delay 0 --wpct 0 --trials 3 --window 0.02)
in_range "loaded est_dram / total at 0% writes (exact)" "$(echo "$r0" | awk -F, '{print $12 / $11}')" 0.9999 1.0001
r1=$(build/loaded.exe --threads 1 --delay 0 --wpct 100 --trials 5 --window 0.02)
in_range "loaded est_dram / (2 x traffic + chase) at 100% writes" \
    "$(echo "$r1" | awk -F, '{print $12 / (2 * $10 + 64 / $6)}')" 0.95 1.05
# loaded: more delay must mean less traffic (the delay loop is not optimized away)
fast=$(build/loaded.exe --threads 1 --delay 0 --trials 3 --window 0.02 | cut -d, -f10)
slow=$(build/loaded.exe --threads 1 --delay 400 --trials 3 --window 0.02 | cut -d, -f10)
in_range "loaded traffic GB/s, delay 0 / delay 400" "$(python -c "print($fast/$slow)")" 3 1000

echo "[6] asm: chase loops"
chase_loop() { awk -v f="$1:" '$0==f{on=1} on' build/asm/lat.s | awk '/^\.L[0-9]+:/{n++} n==1'; }
c1=$(chase_loop chase1)
echo "$c1" | grep -qE 'movq\s+\(%(\w+)\), %\1' && ok "asm: chase1 is a dependent load chain" || bad "asm: chase1 not a dependent chain"
spill12=$(chase_loop chase12 | sed '/jne/q' | grep -c '(%rsp)')
[ "$spill12" -eq 0 ] && ok "asm: chase12 keeps all 12 chains in registers" || bad "asm: chase12 spills ($spill12 stack refs)"

echo "[7] sanity vs known values (takes ~1 minute)"
# DRAM-latency medians drift 10-25% between runs on this laptop (background
# activity, refresh), while the minimum is stable to ~1%. Interference only
# ever adds time, so checks that compare two separate runs use the minimum
# (lat column 10, loaded column 7).
col() { cut -d, -f"$1"; }
l1=$(build/lat.exe --size 16K --trials 7 | col 12)
in_range "L1 latency, cycles (Willow Cove: 5)" "$l1" 4.5 6
# Each run is a new process with different physical pages, and DRAM latency
# varies between processes (seen: loaded idle 112-137 ns, lat 106-115 ns in
# separate runs) even though the minimum over trials is stable within a run.
# Comparing ONE process of each tool tests placement luck, not the tools, so
# cross-tool checks interleave 3 runs of each and compare the lowest minima.
lo() { python -c "import sys; print(min(map(float, sys.argv[1:])))" "$@"; }
d=() i=() a=() b=()
for r in 1 2 3; do
    d+=("$(build/lat.exe --size 256M --trials 9 | col 10)")
    i+=("$(build/loaded.exe --threads 0 --trials 9 | col 7)")
done
dram=$(lo "${d[@]}")
idle=$(lo "${i[@]}")
in_range "DRAM latency (lowest min of 3 runs), ns" "$dram" 60 200
in_range "loaded(idle) / lat latency, lowest min of 3 interleaved runs each" \
    "$(python -c "print($idle/$dram)")" 0.95 1.05
for r in 1 2 3; do
    a+=("$(build/lat.exe --size 256M --k 8 --trials 9 | col 10)")
    b+=("$(build/lat.exe --size 256M --k 1 --trials 9 | col 10)")
done
k8=$(lo "${a[@]}")
k1=$(lo "${b[@]}")
in_range "K=8 / K=1 throughput, lowest min of 3 interleaved runs each (physical max 8)" \
    "$(python -c "print(8 * $k1 / $k8)")" 3 8
bw1=$(build/bw.exe --size 256M --trials 5 | col 11)
in_range "seq read, 1 thread, GB/s" "$bw1" 12 25.6
bw4=$(build/bw.exe --size 64M --threads 4 --trials 5 | col 11)
in_range "seq read, 4 threads, GB/s (<= 25.6 channel peak)" "$bw4" 15 25.6

echo "# total: pass=$pass fail=$fail" >> "$LOG"
echo "total: pass=$pass fail=$fail (log: $LOG)"
[ "$fail" -eq 0 ]
