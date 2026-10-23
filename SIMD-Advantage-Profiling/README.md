# Project #1: SIMD Advantage Profiling (submission)
ECSE 4320/6320 Advanced Computer Systems, class-wide project (110 points).

Single-core study of scalar vs SSE2 / AVX2 / AVX-512 code for five kernels
(saxpy, elementwise multiply, 3-point stencil, dot product plain and
`omp simd`), fp32 and fp64, on an Intel Core i3-1125G4 (Tiger Lake),
Windows 11, gcc 16.2.

**Report: [reports/REPORT.md](reports/REPORT.md)**

## Deliverables
| Deliverable (project overview) | Where |
|---|---|
| Code, build/run scripts, raw measurements, processed data, plotting scripts | `src/`, `scripts/build.sh`, `scripts/run_sweeps.sh`, `data/raw/`, `data/processed/`, `scripts/analyze.py` |
| Methodology: CPU/ISA, compiler and flags, OS, frequency policy, SMT, timing method, repetitions, data initialization | REPORT section 1; `data/raw/env.txt` |
| Plots/tables with units, legends, variability | `plots/` (min..max bands over 3 repetitions), [reports/RESULTS_TABLES.md](reports/RESULTS_TABLES.md) |
| Short report (locality, vector width, arithmetic intensity, bandwidth, anomalies) | [reports/REPORT.md](reports/REPORT.md) |
| AI-use note | [reports/AI-USE.md](reports/AI-USE.md) |
| Pre-experiment hypothesis | [reports/HYPOTHESES.md](reports/HYPOTHESES.md) (committed before any measurement); scored in REPORT section 8 |
| Correctness validation | `scripts/test.sh` (1121 checks vs a long-double reference), `data/raw/correctness.log`; REPORT section 2 |
| Negative / misleading case | REPORT section 7 (featured cases plus a catalog of all cases) |
| Honest anomalies | REPORT section 9 |

## Required experiments
| Experiment | Plot / evidence | Report |
|---|---|---|
| Scalar vs auto-vectorized across working-set sizes (speedup and throughput) | `plots/size_speedup_f32.png`, `size_speedup_f64.png`, `size_gflops_f32.png`, `size_gflops_f64.png` | section 4 |
| Locality sweep, annotated where gains compress | `plots/locality_saxpy_f32.png` | section 4 |
| Alignment and tail handling | `plots/align_f32.png`, `align_f64.png`, `tail.png` | section 5 |
| Stride study | `plots/stride.png` | section 5 |
| Data-type comparison | `plots/dtype_ratio.png` | section 5 |
| Vectorization verification | [reports/VECTORIZATION.md](reports/VECTORIZATION.md) | section 3 |
| Roofline with measured bandwidth and peak FLOP rate | `plots/roofline.png`, `data/raw/peaks.csv` | section 6 |
| Negative example | `plots/dot_negative.png` | section 7 |

## Rubric map (110 points)
| Category | Where |
|---|---|
| Baseline, correctness, reproducibility (15) | REPORT sections 1-2, `scripts/test.sh`, `data/raw/env.txt`, Reproduce below |
| Vectorization verification (15) | REPORT section 3, `reports/VECTORIZATION.md` |
| Locality and memory-bound analysis (20) | REPORT section 4 |
| Alignment, tail, stride/gather (20) | REPORT section 5 |
| Roofline and quantitative insight (20) | REPORT section 6 |
| AI-use transparency and failure case (10) | `reports/AI-USE.md`, REPORT section 7 |
| Report quality (10) | `reports/REPORT.md` (limitations: section 9) |

## Layout
- `src/` kernels (`kernels.c`), timing/validation harness (`bench.c`), roofline ceilings (`peak.c`)
- `scripts/` build, test, sweeps, vectorization check, analysis
- `data/raw/` sweep CSVs, measured peaks, correctness log, environment;
  `recheck_2026-10-22/` holds an independent re-run
- `data/processed/` aggregated tables
- `plots/` figures used by the report
- `reports/` REPORT, RESULTS_TABLES (generated), VECTORIZATION (generated), HYPOTHESES, AI-USE

## Reproduce (Windows, Git Bash + MSYS2 UCRT64 gcc, Python with numpy/pandas/matplotlib)
```bash
scripts/capture_env.sh                  # hardware / OS / compiler -> data/raw/env.txt
scripts/build.sh                        # build/ (not committed): bench_{scalar,sse2,avx2,avx512}.exe, peak.exe, asm + vec reports
scripts/test.sh                         # 1121 correctness checks vs long-double reference
python scripts/verify_vec.py            # vectorization evidence -> reports/VECTORIZATION.md
scripts/run_sweeps.sh all               # ~35 min, keep on AC power; data/raw/sweep_*.csv (QUICK=1 = smoke test)
./build/peak.exe > data/raw/peaks.csv   # measured FLOP/s and bandwidth ceilings
python scripts/analyze.py               # plots/*.png, data/processed/*, reports/RESULTS_TABLES.md
```
