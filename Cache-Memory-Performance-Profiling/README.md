# Project #2: Cache & Memory Performance Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

## Layout
- `src/`            latency (pointer-chase), bandwidth and loaded-latency benchmarks
- `scripts/`        build, test, run, environment-capture and plotting scripts
- `data/raw/`       raw measurements (CSV), counter exports, environment capture
- `data/processed/` aggregated statistics
- `plots/`          generated figures
- `reports/`        hypotheses, analysis, AI-USE.md, REPORT.md

## Quick start (Git Bash + MSYS2 UCRT64 gcc)
```bash
scripts/capture_env.sh > data/raw/env.txt   # hardware / OS / memory / VBS / tools
scripts/build.sh                            # build/{lat,bw,loaded,kern}.exe + asm
scripts/test.sh                             # 366 correctness, regression + sanity checks
scripts/run_sweeps.sh all                   # ~35-45 min, all sweeps + CPU logger (QUICK=1: 90 s smoke run)
scripts/run_counters.sh                     # ADMIN Git Bash only: VTune counter runs (~25 min)
```

## Platform
Windows 11 + MSYS2 UCRT64 gcc; Intel VTune Profiler for hardware counters
(stands in for Linux `perf`). Our own benchmarks replace Intel MLC, which the
rubric allows as "an equivalent memory benchmark".

## Status
- [x] Step 1: repo skeleton and environment capture (`data/raw/env.txt`, `env_notes.txt`)
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [x] Batch 1: benchmarks, build, correctness/sanity tests; VTune smoke test passed and validated
- [~] Batch 2: sweeps done (run 2 + re-measured flags); VTune counter runs pending (admin)
- [ ] Batch 3: analysis, plots, AMAT / Little's Law, pitfall demo
- [ ] Batch 4: AI-USE.md and REPORT.md
