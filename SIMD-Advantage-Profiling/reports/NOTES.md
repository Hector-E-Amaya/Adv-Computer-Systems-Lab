# Working notes (findings to carry into REPORT.md)

## Batch 1 (build + correctness)
- **H8 is half right.** gcc 16.2 -O3 reports the plain `dot` loops as
  "vectorized" in the sse2/avx2/avx512 builds. The assembly
  (`build/asm/kernels_avx2.s`, `dot_f32`) shows an in-order ("fold-left")
  reduction: loads and multiplies are packed (`vmulps ymm`), but the 8 products
  are then added one at a time with a dependent chain of `vaddss`. Strict FP
  order is preserved: the result is bit-identical across all four builds (same
  error, 95.5 eps for fp32 at N=1M). The add-latency chain should still bound
  it near scalar speed. To be confirmed in Batch 2.
- Scalar build: no loop vectorized (`build/vecinfo/kernels_scalar.txt` empty),
  no packed FP arithmetic in `kernels_scalar.s` (checked by `scripts/test.sh`).
- First sample (not a sweep): saxpy fp32 N=4096, avx2 283.7 ns vs scalar
  1497.6 ns, about 5.3x, consistent with H1. Effective clock measured about
  3.65-3.69 GHz.
- Mutation check: injected an off-by-one in saxpy/dot and an overrun in mul.
  All caught at small N, the mul overrun by the guard elements. A single
  missing element in a 1M-element dot product falls inside the (n+8) eps
  worst-case tolerance, so tails are tested at small N.

## Batch 2 (vectorization verification + sweeps)
- `scripts/verify_vec.py` -> `reports/VECTORIZATION.md`: all 48 build x
  function checks pass. Each SIMD build uses its full register width
  (xmm/ymm/zmm) for every unit-stride kernel, and the scalar build has no
  vector code.
- `omp simd` dot is reassociated (packed partial sums) in every SIMD build.
  The plain dot is in-order in every SIMD build.
- **Strided saxpy has no gathers.** It is "vectorized" in every SIMD build, but
  with 0 `vgather`/`vscatter`, even at -march=tigerlake. GCC emulates the
  strided access with scalar loads/stores plus inserts, so expect little or no
  SIMD gain at stride > 1 (H6).
  **Corrected in Batch 4:** this was wrong. GCC versions the loop on
  `stride == 1` (`cmpq $1, %rdx; jne`). Only that case is vectorized, and
  stride > 1 runs a plain scalar loop. Found by reading the loop that
  actually executes, not just the first loop in the function.
- Bench CSV output switched to binary stdout (LF only). Windows text-mode
  stdout was emitting CRLF.
- Quick smoke sweep (3 trials, 5 ms; indicative only):
  - plain dot is 0.94-1.02x scalar in all builds (H8 performance side holds);
  - DRAM speedups are 0.84-1.46x (H2 holds);
  - **H7 looks falsified:** in L1 (N=1024), saxpy f32 avx512 is 13.9x vs avx2
    6.7x. Both run about 1.5 cycles per loop iteration, so the per-iteration
    cost (load/store/uop issue) sets the speed, not FMA throughput, and twice
    the lanes means twice the speed.
- Full sweeps: 6048 rows (size 4440, align 648, tail 696, stride 264), 3 reps,
  0 invalid. Sweep correctness re-verified after the stdout change (1121/1121).
- **Measured peaks (`data/raw/peaks.csv`, `src/peak.c`).** FLOP/cycle:
  - scalar mul+add 2.0: mul and add share ports 0/1, so 2 ops/cycle, not 4;
  - sse2 8.0;
  - FMA ymm 31.7, i.e. 2 FMA units x 8 lanes x 2;
  - **FMA zmm 31.0, same as ymm: one 512-bit FMA unit**.
  Bandwidth (GB/s, read): L1 429, L2 157, L3 51, DRAM 21; DRAM write 8.8,
  copy 13.2.
- Bandwidth test bug found and fixed: one `vaddps` per load capped L1 read at
  ~1 zmm/cycle (207 GB/s, the single 512-bit ALU port, not the cache), which
  was below what saxpy itself achieves (401 GB/s). Fixed by one FMA per two
  loads (like saxpy), giving 429 GB/s, about 2 loads/cycle.
- **Why AVX-512 beats AVX2 2x in L1 despite equal FMA peak:** saxpy and mul
  in L1 are limited by load/store and loop issue per iteration, not FMA
  throughput. Twice the bytes per instruction means twice the speed.
- Why dot_simd is 2.9x even in DRAM (H8 second half wrong): the *scalar* dot is
  add-latency bound at ~4 cycles/element (~7 GB/s). That is far below the
  21 GB/s DRAM roof, so the baseline never becomes memory-bound.
- Alignment: avx512 is 1.7-1.9x slower at any non-64B offset in L1 (every zmm
  access splits a line). avx2 recovers fully at a 32B offset (ymm accesses no
  longer split). One outlier to flag: mul avx2 DRAM at 8B offset, 1.29x.
- Caveat for cycles/element: the frequency is measured with a scalar add
  chain before and after each run. It does not capture any AVX-512 frequency
  licence drop during the timed loop. Seen range: 3.05-3.69 GHz.
