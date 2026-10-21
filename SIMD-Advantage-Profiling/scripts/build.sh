#!/usr/bin/env bash
# Build the four benchmark variants (scalar, sse2, avx2, avx512).
# Also writes, per build:
#   build/asm/kernels_<build>.s        kernel assembly (vectorization evidence)
#   build/vecinfo/kernels_<build>.txt  gcc -fopt-info-vec report (vectorized + missed)
set -euo pipefail
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
mkdir -p build/asm build/vecinfo

# gnu11 keeps GCC's default -ffp-contract=fast (ISO -std=c11 would turn it off);
# stated explicitly so FMA contraction in the avx2/avx512 builds is a visible choice.
COMMON="-O3 -std=gnu11 -ffp-contract=fast -Wall -Wextra -Isrc"

# Win64 GCC cannot guarantee 32/64-byte stack alignment for spilled vector
# registers (GCC PR 54412). Have the assembler emit unaligned moves so a spill
# can never fault. Same speed on aligned data on this CPU.
WINFIX="-Wa,-muse-unaligned-vector-move"

declare -A FLAGS=(
    # No -fopenmp-simd here: GCC vectorizes '#pragma omp simd' loops even under
    # -fno-tree-vectorize, which would make the scalar baseline not scalar.
    [scalar]="-march=x86-64 -fno-tree-vectorize -fno-tree-slp-vectorize -Wno-unknown-pragmas"
    [sse2]="-march=x86-64 -fopenmp-simd"
    [avx2]="-march=x86-64-v3 -fopenmp-simd"
    [avx512]="-march=tigerlake -mprefer-vector-width=512 -fopenmp-simd"
)

for b in scalar sse2 avx2 avx512; do
    f="$COMMON ${FLAGS[$b]} $WINFIX -DBUILD_NAME=\"$b\""
    echo "[$b] $f"
    $CC $f -c src/kernels.c -o build/kernels_$b.o \
        -fopt-info-vec-optimized-missed=build/vecinfo/kernels_$b.txt
    $CC $f -S src/kernels.c -o build/asm/kernels_$b.s
    $CC $f -c src/bench.c -o build/bench_$b.o
    $CC build/kernels_$b.o build/bench_$b.o -o build/bench_$b.exe -lm
    ./build/bench_$b.exe --info
done
echo "build OK"
