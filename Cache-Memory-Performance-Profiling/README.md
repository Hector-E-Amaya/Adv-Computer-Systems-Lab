# Project #2: Cache & Memory Performance Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

**Start here: [reports/REPORT.md](reports/REPORT.md).** All tables:
[reports/RESULTS_TABLES.md](reports/RESULTS_TABLES.md). AI-use note:
[reports/AI-USE.md](reports/AI-USE.md).

## Layout
- `src/`            `lat` (pointer-chase latency), `bw` (bandwidth), `loaded` (latency under load), `kern` (kernels), `common`
- `scripts/`        build, test, sweeps, VTune counter runs, interference check, re-measurement, analysis
- `data/raw/`       sweep CSVs (final run 2), `counters/` (VTune), `run1_interfered/` (rejected run, pitfall evidence), CPU logs, env capture
- `data/processed/` aggregated tables (CSV)
- `plots/`          figures
- `reports/`        REPORT, RESULTS_TABLES, HYPOTHESES (+ REVIEW), METHODS, NOTES, CONTRIBUTIONS, AI-USE

## Platform
Windows 11 + MSYS2 UCRT64 gcc; Intel VTune Profiler for hardware counters
(stands in for Linux `perf`). Our own benchmarks replace Intel MLC, which the
rubric allows as "an equivalent memory benchmark".

## Reproduce (Git Bash; Python with matplotlib; Intel VTune for counters)
```bash
scripts/capture_env.sh > data/raw/env.txt   # hardware / OS / memory / VBS / tools
scripts/build.sh                            # build/{lat,bw,loaded,kern}.exe + asm
scripts/test.sh                             # 366 correctness, regression + sanity checks
scripts/run_sweeps.sh all                   # ~22 min with the machine idle; typeperf CPU log (QUICK=1: 90 s smoke run)
python scripts/check_interference.py        # flag rows measured during other CPU activity
python scripts/remeasure_flagged.py         # re-measure flagged latency-sensitive rows
scripts/run_counters.sh                     # ADMIN Git Bash only: 45 VTune runs (~35 min)
python scripts/analyze.py                   # plots/, data/processed/, reports/RESULTS_TABLES.md
```

## Rubric map (185 points)
| Category | Where |
|---|---|
| Zero-queue baselines (25) | REPORT section 2, `plots/latency_working_set.png`, RESULTS_TABLES 1 and 7 (pure latencies) |
| Pattern / granularity / RW sweeps (35) | REPORT section 3, `plots/bandwidth_patterns.png`, `plots/bandwidth_working_set.png`, RESULTS_TABLES 5 |
| Throughput-latency knee (35) | REPORT section 4, `plots/knee.png`, `plots/mlp.png`, RESULTS_TABLES 3-4 |
| Working-set / cache / TLB analysis (35) | REPORT section 5, `plots/counters_kernels.png`, `plots/amat.png`, `plots/tlb.png`, RESULTS_TABLES 2, 6, 7 |
| Experimental rigor (25) | REPORT section 1, `reports/METHODS.md`, `scripts/test.sh`, `data/raw/env.txt`, counter validation (RESULTS_TABLES 8) |
| AI use + pitfall (15) | `reports/AI-USE.md`, REPORT section 6, `plots/pitfall_sequential.png`, `data/raw/run1_interfered/` |
| Report quality (15) | `reports/REPORT.md` (limitations: section 8) |
| Pre-experiment hypotheses | `reports/HYPOTHESES.md` (committed first), scorecard in REPORT section 7 |

## Status
- [x] Step 1: environment capture; Step 2: hypotheses (+ pre-sweep review)
- [x] Batch 1: tools, build, 366-check test suite, VTune smoke test validated
- [x] Batch 2: sweeps (quiet run 2 + re-measured flags), 45 VTune counter runs
- [x] Batch 3: analysis, plots, AMAT / Little's Law, pitfalls
- [x] Batch 4: REPORT.md, AI-USE.md, rubric map
