#!/usr/bin/env bash
# Correctness tests: every build x kernel x dtype against the long-double
# reference in bench.c, over sizes that exercise vector tails, plus misaligned
# and strided cases. Also checks the scalar build contains no packed FP math.
# Log: data/raw/correctness.log. Exit status is nonzero if anything fails.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=data/raw/correctness.log
builds=(scalar sse2 avx2 avx512)
kernels=(saxpy mul stencil dot dot_simd)
sizes=(1 2 3 7 15 16 17 31 33 63 64 65 127 129 1001 4096 100003 1048576)
pass=0
fail=0

{
    echo "# correctness run $(date -u +%Y-%m-%dT%H:%M:%SZ), commit $(git rev-parse --short HEAD)"
    echo "# err and tol in units of eps * sum|terms|"
} > "$LOG"

run() {
    local out
    out=$("$@" --validate 2>&1)
    if [ $? -eq 0 ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAILED: $*"
    fi
    echo "$out" >> "$LOG"
}

for b in "${builds[@]}"; do
    exe=build/bench_$b.exe
    [ -x "$exe" ] || { echo "missing $exe (run scripts/build.sh)"; exit 1; }
    for dt in f32 f64; do
        es=4; [ "$dt" = f64 ] && es=8
        for k in "${kernels[@]}"; do
            for n in "${sizes[@]}"; do
                run "$exe" --kernel "$k" --dtype "$dt" --n "$n"
            done
            for m in "$es" $((3 * es)) 32; do
                for n in 65 1001; do
                    run "$exe" --kernel "$k" --dtype "$dt" --n "$n" --misalign "$m"
                done
            done
        done
        for s in 2 3 8 16 64; do
            for n in 1 7 1001 65536; do
                run "$exe" --kernel saxpy --dtype "$dt" --n "$n" --stride "$s"
            done
        done
    done
    echo "[$b] done (pass=$pass fail=$fail so far)"
done

# Scalar baseline must contain no packed floating-point arithmetic.
packed=$(grep -Ec '\bv?(add|sub|mul|div)p[sd]\b|\bvfn?m(add|sub)[0-9]+p[sd]\b' build/asm/kernels_scalar.s)
if [ "$packed" -eq 0 ]; then
    pass=$((pass + 1))
    echo "PASS scalar build: no packed FP arithmetic in kernels" >> "$LOG"
else
    fail=$((fail + 1))
    echo "FAIL scalar build: $packed packed FP instructions in kernels" | tee -a "$LOG"
fi

echo "# total: pass=$pass fail=$fail" >> "$LOG"
echo "total: pass=$pass fail=$fail (log: $LOG)"
[ "$fail" -eq 0 ]
