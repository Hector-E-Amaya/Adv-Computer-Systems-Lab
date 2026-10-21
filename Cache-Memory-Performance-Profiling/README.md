# Project #2: Cache & Memory Performance Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

## Layout
- `src/`            latency (pointer-chase), bandwidth and loaded-latency benchmarks
- `scripts/`        build, test, run, environment-capture and plotting scripts
- `data/raw/`       raw measurements (CSV), counter exports, environment capture
- `data/processed/` aggregated statistics
- `plots/`          generated figures
- `reports/`        hypotheses, analysis, AI-USE.md, REPORT.md

## Platform
Windows 11 + MSYS2 UCRT64 gcc; Intel VTune Profiler for hardware counters
(stands in for Linux `perf`). Our own benchmarks replace Intel MLC, which the
rubric allows as "an equivalent memory benchmark".

## Status
- [x] Step 1: repo skeleton and environment capture (`data/raw/env.txt`, `env_notes.txt`)
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [ ] Batch 1: benchmarks, build, correctness/sanity tests
- [ ] Batch 2: sweeps + VTune counter runs
- [ ] Batch 3: analysis, plots, AMAT / Little's Law, pitfall demo
- [ ] Batch 4: AI-USE.md and REPORT.md
