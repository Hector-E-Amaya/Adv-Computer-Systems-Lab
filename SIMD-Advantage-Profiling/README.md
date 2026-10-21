# Project #1: SIMD Advantage Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

**Start here: [reports/REPORT.md](reports/REPORT.md)** (short report).
Full analysis: [reports/ANALYSIS.md](reports/ANALYSIS.md). AI-use note:
[reports/AI-USE.md](reports/AI-USE.md).

## Layout
- `src/`            kernels (`kernels.c`), timing/validation harness (`bench.c`), roofline ceilings (`peak.c`)
- `scripts/`        build, test, run, environment-capture, verification and plotting scripts
- `data/raw/`       raw measurements (CSV), correctness log, environment capture
- `data/processed/` aggregated statistics
- `plots/`          generated figures
- `reports/`        report, analysis, hypotheses, vectorization evidence, results tables, notes, AI-use note

## Reproduce (Windows, Git Bash + MSYS2 UCRT64 gcc, Python with numpy/pandas/matplotlib)
```bash
scripts/capture_env.sh                  # hardware / OS / compiler -> data/raw/env.txt
scripts/build.sh                        # build/bench_{scalar,sse2,avx2,avx512}.exe, build/peak.exe, asm + vec reports
scripts/test.sh                         # 1121 correctness checks vs long-double reference
python scripts/verify_vec.py            # vectorization evidence -> reports/VECTORIZATION.md
scripts/run_sweeps.sh all               # ~35 min, keep on AC power; data/raw/sweep_*.csv (QUICK=1 = smoke test)
./build/peak.exe > data/raw/peaks.csv   # measured FLOP/s and bandwidth ceilings
python scripts/analyze.py               # plots/*.png, data/processed/*, reports/RESULTS_TABLES.md
```

## Rubric map
| Rubric item | Where |
|---|---|
| Baseline, correctness, reproducibility (15) | `scripts/test.sh`, `data/raw/correctness.log`, `data/raw/env.txt`, this README, REPORT §1-2 |
| Vectorization verification (15) | `reports/VECTORIZATION.md`, `build/asm/`, `build/vecinfo/`, REPORT §3 |
| Locality and memory-bound analysis (20) | `plots/locality_saxpy_f32.png`, `plots/size_*`, REPORT §4, ANALYSIS §2 |
| Alignment, tail, stride/gather (20) | `plots/align_*`, `plots/tail.png`, `plots/stride.png`, `plots/dtype_ratio.png`, REPORT §5 |
| Roofline and quantitative insight (20) | `plots/roofline.png`, `data/raw/peaks.csv`, REPORT §6, ANALYSIS §6 |
| AI-use transparency and failure case (10) | `reports/AI-USE.md`, `plots/dot_negative.png`, REPORT §7 |
| Report quality (10) | `reports/REPORT.md` (limitations in §8) |
| Pre-experiment hypothesis | `reports/HYPOTHESES.md` (committed before any measurement), verdicts in ANALYSIS summary table |

## Status
- [x] Step 1: repo skeleton and environment capture
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [x] Batch 1: kernels, harness, build, correctness tests
- [x] Batch 2: vectorization verification and experiment sweeps
- [x] Batch 3: analysis, plots, roofline, negative case
- [x] Batch 4: REPORT.md, AI-USE.md, plot polish
