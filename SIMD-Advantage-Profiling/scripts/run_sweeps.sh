#!/usr/bin/env bash
# Experiment sweeps. Each sweep writes data/raw/sweep_<name>.csv
# (bench CSV columns prefixed with "rep").
#
#   scripts/run_sweeps.sh [size|align|tail|stride|all ...]
#
# Env: REPS (default 3) full repetitions of each sweep; QUICK=1 for a short
# smoke test (fewer points, 1 rep, 5 ms trials, writes *_quick.csv).
#
# Order: the repetition loop is outermost and the build loop innermost, so
# slow drift (thermals, turbo, background load) hits all builds alike instead
# of biasing whichever build ran last.
set -uo pipefail
cd "$(dirname "$0")/.."

BUILDS=(scalar sse2 avx2 avx512)
KERNELS=(saxpy mul stencil dot dot_simd)
REPS=${REPS:-3}
QUICK=${QUICK:-0}
EXTRA=()
SUFFIX=""
if [ "$QUICK" = 1 ]; then
    REPS=1
    EXTRA=(--min-time 0.005 --trials 3)
    SUFFIX="_quick"
fi

for b in "${BUILDS[@]}"; do
    [ -x "build/bench_$b.exe" ] || { echo "missing build/bench_$b.exe (run scripts/build.sh)"; exit 1; }
done

OUT=""
invalid=0
start_csv() {
    OUT="data/raw/sweep_$1$SUFFIX.csv"
    echo "rep,$(build/bench_scalar.exe --header)" > "$OUT"
    echo "== $1 -> $OUT ($(date +%H:%M:%S))"
}

# one point: rep build args...
point() {
    local rep=$1 b=$2
    shift 2
    local row
    row=$("build/bench_$b.exe" "$@" "${EXTRA[@]}")
    [ $? -eq 0 ] || { invalid=$((invalid + 1)); echo "INVALID: $b $*"; }
    echo "$rep,$row" >> "$OUT"
}

# N = 2^k and 1.5 * 2^k from 64 to 16M elements: covers L1 (48 KB),
# L2 (1.25 MB), L3 (8 MB) and DRAM for every kernel and dtype.
size_points() {
    local k
    for ((k = 6; k <= 24; k++)); do
        echo $((1 << k))
        [ $k -lt 24 ] && echo $((3 << (k - 1)))
    done
}

sweep_size() {
    start_csv size
    local sizes
    if [ "$QUICK" = 1 ]; then sizes=(1024 65536 4194304); else mapfile -t sizes < <(size_points); fi
    for ((rep = 1; rep <= REPS; rep++)); do
        for dt in f32 f64; do
            for k in "${KERNELS[@]}"; do
                for n in "${sizes[@]}"; do
                    for b in "${BUILDS[@]}"; do
                        point $rep $b --kernel $k --dtype $dt --n $n
                    done
                done
            done
            echo "   rep $rep $dt done ($(date +%H:%M:%S))"
        done
    done
}

# Byte offsets from a 64-byte boundary, applied to every array; sizes in
# L1, L2 and DRAM.
sweep_align() {
    start_csv align
    local sizes=(2048 65536 4194304)
    [ "$QUICK" = 1 ] && sizes=(2048)
    for ((rep = 1; rep <= REPS; rep++)); do
        for dt in f32 f64; do
            local offs=(0 8 16 32)
            [ $dt = f32 ] && offs=(0 4 8 16 32)
            for k in saxpy mul; do
                for n in "${sizes[@]}"; do
                    for m in "${offs[@]}"; do
                        for b in "${BUILDS[@]}"; do
                            point $rep $b --kernel $k --dtype $dt --n $n --misalign $m
                        done
                    done
                done
            done
        done
    done
}

# Small N: prologue/epilogue (vector tail) overhead, fp32 saxpy.
sweep_tail() {
    start_csv tail
    local sizes=($(seq 1 48) $(seq 56 8 128))
    [ "$QUICK" = 1 ] && sizes=(1 7 16 17 64)
    for ((rep = 1; rep <= REPS; rep++)); do
        for n in "${sizes[@]}"; do
            for b in "${BUILDS[@]}"; do
                point $rep $b --kernel saxpy --dtype f32 --n $n
            done
        done
    done
}

# Strided saxpy: N=1024 (in cache for every stride) and N=2M (DRAM even at
# stride 1). fp64 stops at 16 so the largest arrays stay at 256 MB.
sweep_stride() {
    start_csv stride
    local sizes=(1024 2097152)
    [ "$QUICK" = 1 ] && sizes=(1024)
    for ((rep = 1; rep <= REPS; rep++)); do
        for dt in f32 f64; do
            local strides=(1 2 4 8 16 32)
            [ $dt = f64 ] && strides=(1 2 4 8 16)
            for n in "${sizes[@]}"; do
                for s in "${strides[@]}"; do
                    for b in "${BUILDS[@]}"; do
                        point $rep $b --kernel saxpy --dtype $dt --n $n --stride $s
                    done
                done
            done
        done
    done
}

sweeps=("$@")
[ ${#sweeps[@]} -eq 0 ] && sweeps=(all)
[ "${sweeps[0]}" = all ] && sweeps=(size align tail stride)
for s in "${sweeps[@]}"; do
    case $s in
        size | align | tail | stride) sweep_$s ;;
        *) echo "unknown sweep: $s"; exit 2 ;;
    esac
done
echo "finished $(date +%H:%M:%S); invalid rows: $invalid"
[ "$invalid" -eq 0 ]
