#!/usr/bin/env bash
# Batch 2 measurement sweeps (no administrator rights needed).
#
#   scripts/run_sweeps.sh [ws|bw|bwws|knee|mlp|tlb|pin|all ...]
#
#   ws    zero-queue working-set latency: lat random + seq (pitfall), 4 KB .. 512 MB
#   bw    pattern x granularity x R/W mix x NT, 1 and 4 threads, DRAM-resident
#   bwws  bandwidth working-set sweep: seq and random read, 16 KB .. 512 MB
#   knee  loaded latency: traffic threads x injection delay, plus R/W-mix traffic
#   mlp   K independent chases, K = 1..16 (Little's Law, single thread)
#   tlb   page mode (one line per 4 KB page) + compact control, 16 .. 65536 pages
#   pin   pitfall: pinned vs unpinned DRAM latency, alternating
#
# Output: data/raw/sweep_<name>.csv = rep,t_start,t_end + the tool's own CSV
# columns. t_start/t_end are epoch seconds, used to match each row against
# data/raw/cpu_log.csv (typeperf, all CPUs, 1 s), which runs during the sweeps
# so points disturbed by other activity can be flagged.
#
# Env: REPS (default 3) full repetitions; QUICK=1 for a short smoke run
# (1 rep, few points, writes *_quick.csv and cpu_log_quick.csv).
# Order (METHODS.md): repetition loop outermost, variants innermost. Idle
# latency points get a 2 s pause first. Every tool's exit status is checked;
# invalid rows are counted and the script exits nonzero if there are any.
set -uo pipefail
cd "$(dirname "$0")/.."

REPS=${REPS:-3}
QUICK=${QUICK:-0}
SUFFIX=""
if [ "$QUICK" = 1 ]; then
    REPS=1
    SUFFIX="_quick"
fi
for t in lat bw loaded; do
    [ -x build/$t.exe ] || { echo "FAIL: build/$t.exe missing (run scripts/build.sh)"; exit 1; }
done

invalid=0
OUT=""
LOGPID=""

stop_logger() {
    if [ -n "$LOGPID" ]; then
        kill "$LOGPID" 2>/dev/null
        wait "$LOGPID" 2>/dev/null
        LOGPID=""
    fi
}
trap 'echo "INTERRUPTED: sweeps stopped before finishing; partial CSVs are incomplete."; stop_logger; exit 130' INT TERM HUP

start_logger() {
    local f=data/raw/cpu_log$SUFFIX.csv
    # typeperf (built into Windows): % busy per logical CPU, once per second.
    typeperf '\Processor(*)\% Processor Time' -si 1 -f CSV -o "$f" -y > /dev/null 2>&1 &
    LOGPID=$!
    sleep 2
    if ! kill -0 "$LOGPID" 2>/dev/null; then
        echo "FAIL: CPU logger (typeperf) did not start"
        exit 1
    fi
    echo "CPU logger running -> $f"
}

begin() { # begin NAME TOOL
    OUT=data/raw/sweep_$1$SUFFIX.csv
    echo "rep,t_start,t_end,$(build/$2.exe --header)" > "$OUT"
    echo "== $1 -> $OUT ($(date +%H:%M:%S))"
}

# point REP TOOL ARGS...: run one configuration, append rep,t_start,t_end,row
point() {
    local rep=$1 tool=$2
    shift 2
    local t0 t1 row status
    t0=$(date +%s.%3N)
    row=$(build/$tool.exe "$@")
    status=$?
    t1=$(date +%s.%3N)
    if [ "$status" -ne 0 ] || [ -z "$row" ]; then
        invalid=$((invalid + 1))
        echo "INVALID (exit $status): $tool $*"
        return
    fi
    echo "$rep,$t0,$t1,$row" >> "$OUT"
}

# 4 KB .. 512 MB: powers of two and 1.5x steps.
ws_sizes() {
    local k
    for ((k = 12; k <= 29; k++)); do
        echo $((1 << k))
        [ $k -lt 29 ] && echo $((3 << (k - 1)))
    done
}

sweep_ws() {
    begin ws lat
    local sizes
    if [ "$QUICK" = 1 ]; then sizes=(16384 1048576 268435456); else mapfile -t sizes < <(ws_sizes); fi
    for ((rep = 1; rep <= REPS; rep++)); do
        for s in "${sizes[@]}"; do
            for m in random seq; do point $rep lat --size $s --mode $m; done
        done
        echo "   rep $rep done ($(date +%H:%M:%S))"
    done
}

sweep_bw() {
    begin bw bw
    local pats=("seq" "stride --stride 256" "stride --stride 1024" "random") grans=(8 64) mixes=(0 30 50 100)
    [ "$QUICK" = 1 ] && pats=("seq" "random") && mixes=(0 50)
    for ((rep = 1; rep <= REPS; rep++)); do
        for th in 1 4; do
            local size=256M
            [ $th = 4 ] && size=64M # per thread: 256 MB total, still DRAM-resident
            for p in "${pats[@]}"; do
                for w in "${mixes[@]}"; do
                    for gr in "${grans[@]}"; do
                        point $rep bw --size $size --threads $th --pattern $p --wpct $w --gran $gr
                    done
                    # non-temporal stores only where there are writes, full lines only
                    [ "$w" -gt 0 ] && point $rep bw --size $size --threads $th --pattern $p --wpct $w --gran 64 --nt
                done
            done
        done
        echo "   rep $rep done ($(date +%H:%M:%S))"
    done
}

sweep_bwws() {
    begin bwws bw
    local sizes=() k
    for ((k = 14; k <= 29; k++)); do sizes+=($((1 << k))); done
    [ "$QUICK" = 1 ] && sizes=(16384 1048576 268435456)
    for ((rep = 1; rep <= REPS; rep++)); do
        for s in "${sizes[@]}"; do
            for p in seq random; do point $rep bw --size $s --pattern $p; done
        done
    done
}

sweep_knee() {
    begin knee loaded
    local delays=(0 1 2 5 10 20 50 100 200 500 1000 2000) threads=(1 2 3 5 6) mixes=(0 30 50 100)
    [ "$QUICK" = 1 ] && delays=(0 100) && threads=(1 3) && mixes=(0 100)
    for ((rep = 1; rep <= REPS; rep++)); do
        sleep 2 # idle point never right after heavy traffic (METHODS.md)
        point $rep loaded --threads 0
        for d in "${delays[@]}"; do
            for th in "${threads[@]}"; do
                local smt=""
                [ "$th" -gt 3 ] && smt="--smt"
                point $rep loaded --threads $th --delay $d $smt
            done
        done
        # latency under each R/W mix of the traffic, at full intensity
        for w in "${mixes[@]}"; do point $rep loaded --threads 3 --delay 0 --wpct $w; done
        sleep 2
        point $rep loaded --threads 0
        echo "   rep $rep done ($(date +%H:%M:%S))"
    done
}

sweep_mlp() {
    begin mlp lat
    local ks=(1 2 3 4 6 8 10 12 14 16)
    [ "$QUICK" = 1 ] && ks=(1 8)
    for ((rep = 1; rep <= REPS; rep++)); do
        for k in "${ks[@]}"; do point $rep lat --size 256M --k $k; done
    done
}

sweep_tlb() {
    begin tlb lat
    local pages=(16 32 48 64 96 128 256 512 768 1024 1536 2048 3072 4096 8192 16384 32768 65536)
    [ "$QUICK" = 1 ] && pages=(64 2048 8192)
    for ((rep = 1; rep <= REPS; rep++)); do
        for p in "${pages[@]}"; do
            point $rep lat --size $((p * 4096)) --mode page        # one line per page
            point $rep lat --size $((p * 64)) --mode random        # control: same nodes, compact
        done
        echo "   rep $rep done ($(date +%H:%M:%S))"
    done
    if build/lat.exe --size 4M --mode page --large --check > /dev/null 2>&1; then
        echo "   large pages available: adding 2 MB-page runs"
        for ((rep = 1; rep <= REPS; rep++)); do
            for p in "${pages[@]}"; do point $rep lat --size $((p * 4096)) --mode page --large; done
        done
    else
        echo "   large pages unavailable (SeLockMemoryPrivilege not granted): 4 KB pages only"
    fi
}

sweep_pin() {
    begin pin lat
    local n=15
    [ "$QUICK" = 1 ] && n=2
    for ((i = 1; i <= n; i++)); do
        point $i lat --size 256M            # pinned (default: CPU 2)
        point $i lat --size 256M --cpu -1   # unpinned: the OS may migrate the thread
    done
}

sweeps=("$@")
[ ${#sweeps[@]} -eq 0 ] && sweeps=(all)
[ "${sweeps[0]}" = all ] && sweeps=(ws tlb mlp pin bwws bw knee)
for s in "${sweeps[@]}"; do
    case $s in ws | bw | bwws | knee | mlp | tlb | pin) ;; *) echo "unknown sweep: $s"; exit 2 ;; esac
done

echo "sweeps: ${sweeps[*]} (REPS=$REPS QUICK=$QUICK) started $(date +%H:%M:%S)"
start_logger
for s in "${sweeps[@]}"; do sweep_$s; done
stop_logger
echo "finished $(date +%H:%M:%S); invalid rows: $invalid"
[ "$invalid" -eq 0 ]
