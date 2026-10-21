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
