#!/usr/bin/env bash
# Build the memory benchmarks into build/:
#   lat.exe     pointer-chase latency (random / seq / page modes, K chains)
#   bw.exe      bandwidth by pattern, stride, granularity, read/write mix
#   loaded.exe  latency under adjustable background traffic
#   kern.exe    saxpy / mul kernels for counter correlation
#   lat_mutant.exe  lat with a deliberately broken cycle builder (test only)
# plus build/asm/*.s for inspecting the timed loops.
set -euo pipefail
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
mkdir -p build/asm

# -O2 + AVX2: the timed loops are pointer chases and explicit intrinsics, so
# wider auto-vectorization is not what is being measured.
# Win64 GCC cannot guarantee 32-byte stack alignment for spilled vectors
# (GCC PR 54412); the assembler flag makes such spills unaligned-safe.
# -g: symbols so VTune reports function names. Verified to leave the machine
# code unchanged (objdump of all five objects identical with/without -g).
FLAGS="-O2 -g -std=gnu11 -march=x86-64-v3 -Wall -Wextra -Wa,-muse-unaligned-vector-move -Isrc"

# test.sh checks that kern's saxpy and mul are both vectorized (ymm).
for t in lat bw loaded kern; do
    echo "[$t]"
    $CC $FLAGS src/common.c src/$t.c -o build/$t.exe -lm
    $CC $FLAGS -S src/$t.c -o build/asm/$t.s
done
echo "[lat_mutant, lat_slotmutant]"
$CC $FLAGS -DMUTANT_FISHER_YATES src/common.c src/lat.c -o build/lat_mutant.exe
$CC $FLAGS -DMUTANT_OLD_SLOT src/common.c src/lat.c -o build/lat_slotmutant.exe
echo "build OK"
