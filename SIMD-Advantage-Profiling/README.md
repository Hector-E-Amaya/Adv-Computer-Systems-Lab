# Project #1: SIMD Advantage Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

## Layout
- `src/`            kernels (`kernels.c`) and timing/validation harness (`bench.c`)
- `scripts/`        build, test, run, and environment-capture scripts
- `data/raw/`       raw measurements (CSV), correctness log, environment capture
- `data/processed/` aggregated statistics
- `plots/`          generated figures
- `reports/`        hypotheses, analysis, working notes, AI-USE.md, REPORT.md

## Quick start (Windows, Git Bash + MSYS2 UCRT64 gcc)
```bash
scripts/build.sh   # builds build/bench_{scalar,sse2,avx2,avx512}.exe
scripts/test.sh    # validates every build/kernel/dtype vs a long-double reference
python scripts/verify_vec.py       # vectorization evidence -> reports/VECTORIZATION.md
scripts/run_sweeps.sh all          # ~35 min; data/raw/sweep_*.csv (QUICK=1 for a smoke test)
./build/peak.exe > data/raw/peaks.csv   # measured FLOP/s and bandwidth ceilings
python scripts/analyze.py          # plots/*.png, data/processed/*, reports/RESULTS_TABLES.md
```

## Status
- [x] Step 1: repo skeleton and environment capture
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [x] Batch 1: kernels, harness, build, correctness tests
- [x] Batch 2: vectorization verification and experiment sweeps
- [x] Batch 3: analysis, plots, roofline, negative case
- [ ] Batch 4: AI-USE.md and REPORT.md
