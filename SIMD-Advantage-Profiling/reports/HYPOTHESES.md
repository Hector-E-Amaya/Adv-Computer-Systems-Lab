# Pre-experiment Hypotheses

Written and committed **before any benchmark was run**; the git history is the
evidence of ordering. Numbers are estimates from hardware specs and general
architecture knowledge, not measurements.

## Machine (from data/raw/env.txt)
| Item | Value |
|---|---|
| CPU | Intel Core i3-1125G4 (Tiger Lake), 4 cores / 8 threads, 2.0 GHz base |
| SIMD | SSE4.2, AVX2, FMA, AVX-512 (F/DQ/BW/VL and others) |
| L1D / L2 / L3 | 48 KB per core / 1.25 MB per core / 8 MB shared |
| RAM | 8 GB DDR4-3200 (one module listed, so probably single-channel) |
| OS / compiler | Windows 11, gcc 16.2.0 (MSYS2 UCRT64) |

## Builds compared
| Build | Flags (beyond -O3) | Vector width |
|---|---|---|
| scalar | -march=x86-64 -fno-tree-vectorize -fno-tree-slp-vectorize | none |
| sse2 | -march=x86-64 (default target) | 128-bit |
| avx2 | -march=x86-64-v3 (AVX2 + FMA) | 256-bit |
| avx512 | -march=tigerlake -mprefer-vector-width=512 | 512-bit |

## Kernels and arithmetic intensity (AI)
AI = FLOPs per byte of useful traffic (write-allocate ignored).

| Kernel | FLOPs/elem | Bytes/elem (fp32) | AI fp32 | AI fp64 |
|---|---|---|---|---|
| SAXPY `y = a*x + y` | 2 | 12 | 0.17 | 0.08 |
| Dot product `s += x*y` | 2 | 8 | 0.25 | 0.13 |
| Multiply `z = x*y` | 1 | 12 | 0.08 | 0.04 |
| 3-point stencil | 5 | 8 | 0.63 | 0.31 |

## Peak estimates (single core; to be replaced by measured values)
- Scalar: about 4 FLOP/cycle (2 FMA ports x 2 FLOPs).
- SIMD fp32: about 32 FLOP/cycle for AVX2 (2 x 8 lanes x 2). As far as I know
  AVX-512 on this client part has one fused 512-bit FMA unit, so also ~32.
  At a sustained ~3.5 GHz that is roughly 110 GFLOP/s.
- DRAM: DDR4-3200 is 25.6 GB/s per channel in theory; expect 10-20 GB/s
  measured from one core.
- Roofline ridge point roughly 110 / 15, about 7 FLOP/byte.

## Hypotheses
**H1: SIMD speedup is largest in cache.** fp32 SAXPY avx2 vs scalar in L1:
4-8x. 8 lanes per instruction, but load/store port throughput caps the gain
below the ideal 8x. sse2 should land around 2-4x.

**H2: SIMD gains compress outside cache.** Speedup falls toward 1x in DRAM
(about 1.1-1.6x): both builds become memory-bandwidth-bound.

**H3: Transitions near cache capacities.** For SAXPY/dot fp32 (2 arrays,
8 B/element): drops near N of about 6K (L1), 160K (L2), and 1M (L3, likely
earlier because L3 is shared).

**H4: fp64 halves element throughput in cache**, because of half as many lanes.
In DRAM both types hit the same GB/s, so elements/s still differ by about 2x.

**H5: Alignment matters a little; tails only for small N.** Up to 10-30%
slowdown in L1/L2 when arrays are shifted off a 64-byte boundary (split cache
line loads), larger for 512-bit vectors, near zero in DRAM. Prologue/epilogue
cost visible only for N below about 100 (inflated cycles/element).

**H6: Strided access destroys SIMD benefit.** Useful bandwidth falls roughly
as 1/stride until the stride reaches a cache line (16 floats), then flattens.
SIMD speedup approaches 1x for stride >= 2.

**H7: AVX-512 gives little over AVX2 on this CPU.** At most about 1.2x in L1
and none in DRAM (one 512-bit FMA unit, more misalignment sensitivity).

**H8: Negative example, the dot product.** The plain dot product will NOT
vectorize in any build: FP addition is not associative, so without
-ffast-math the compiler must keep the serial order. Expect about 1x vs
scalar. The `omp simd reduction` version should vectorize, but in DRAM it
should be memory-bound with little extra speedup.

**H9: Roofline.** All kernels have AI well below the ridge point (0.04-0.63 vs
~7), so they should sit on the memory-bound slope when DRAM-resident and only
approach compute or load/store limits in L1.

## Expected noise and anomalies
- Laptop thermal throttling and turbo variation over long runs.
- Windows scheduler noise; SMT sibling interference.
- Higher variance in the DRAM regime than in cache.
- No perf counters on Windows: cycles/element come from time multiplied by a
  measured effective frequency (dependent-add calibration loop).

## What would falsify these
- avx2 speedup in L1 under 2x or over 8x.
- A clean 2x gain from AVX-512 over AVX2.
- The plain dot product auto-vectorizing at -O3.
Any of these would be reported and explained, not hidden.
