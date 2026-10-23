# Project #2: Cache & Memory Performance Profiling (submission)
ECSE 4320/6320 Advanced Computer Systems, class-wide project (185 points).

Latency, bandwidth, loaded latency, cache misses and TLB behaviour of an
Intel Core i3-1125G4 (Tiger Lake) with single-channel DDR4-3200, Windows 11.
Own benchmarks (`lat`, `bw`, `loaded`, `kern`) replace Intel MLC, which the
project overview allows ("or an equivalent memory benchmark"). Intel VTune
supplies the hardware counters, since Linux `perf` is unavailable.

**Report: [reports/REPORT.md](reports/REPORT.md)**

## Deliverables
| Deliverable (project overview) | Where |
|---|---|
| Scripts/commands, raw data, processed data, plotting code | `src/`, `scripts/`, `data/raw/` (sweeps, `counters/` VTune output), `data/processed/`, `scripts/analyze.py` |
| System configuration: CPU, memory, OS, tool versions, frequency policy, SMT, pinning, NUMA | REPORT section 1; `data/raw/env.txt` |
| Plots/tables with units, repeated trials, error bars | `plots/` (min..max over 3 processes), [reports/RESULTS_TABLES.md](reports/RESULTS_TABLES.md) |
| Analysis tied to latency, bandwidth, concurrency, counters, AMAT, Little's Law | REPORT sections 2-5 |
| AI-use note | [reports/AI-USE.md](reports/AI-USE.md) (includes the student's review contributions) |
| Pre-experiment hypothesis | [reports/HYPOTHESES.md](reports/HYPOTHESES.md) (committed before any benchmark); scored in REPORT section 7 |
| Correctness validation | `scripts/test.sh` (366 checks: single-cycle chains, set balance, coverage, failing mutants), `data/raw/correctness.log`; counter validation in REPORT section 1 |
| Negative / misleading case | REPORT section 6 (featured cases plus a catalog of all cases) |
| Honest anomalies | REPORT section 8 |

## Required experiments
| Experiment | Plot / evidence | Report |
|---|---|---|
| Zero-queue per-level latency table (ns and cycles) | `plots/latency_working_set.png`, RESULTS_TABLES 1 | section 2 |
| Pattern and granularity sweep | `plots/bandwidth_patterns.png`, RESULTS_TABLES 5 | section 3 |
| Read/write mix sweep (100/0, 70/30, 50/50, 0/100) | `plots/bandwidth_patterns.png`, knee R/W table (RESULTS_TABLES 4) | sections 3-4 |
| Intensity sweep: throughput vs latency, knee, Little's Law | `plots/knee.png`, `plots/mlp.png`, RESULTS_TABLES 3-4 | section 4 |
| Working-set sweep with L1/L2/LLC/DRAM transitions | `plots/latency_working_set.png`, `plots/bandwidth_working_set.png` | sections 2 and 5 |
| Cache-miss impact (counters vs runtime) | `plots/counters_kernels.png`, `plots/amat.png`, RESULTS_TABLES 6-7 | section 5 |
| TLB-miss impact (page locality; huge pages not available) | `plots/tlb.png`, RESULTS_TABLES 2 | section 5 |
| Benchmark pitfall | `plots/pitfall_sequential.png`, `data/raw/run1_interfered/` | section 6 |

## Rubric map (185 points)
| Category | Where |
|---|---|
| Zero-queue baselines (25) | REPORT section 2 |
| Pattern / granularity / RW sweeps (35) | REPORT section 3 |
| Throughput-latency knee (35) | REPORT section 4 |
| Working-set / cache / TLB analysis (35) | REPORT section 5 |
| Experimental rigor (25) | REPORT section 1 (configuration, procedures, counter validation), `scripts/test.sh` |
| AI use + pitfall (15) | `reports/AI-USE.md`, REPORT section 6 |
| Report quality (15) | `reports/REPORT.md` (limitations: section 8) |

## Layout
- `src/` `lat` (pointer-chase latency), `bw` (bandwidth), `loaded` (latency under load), `kern` (kernels), `common`
- `scripts/` build, test, sweeps, VTune counter runs, interference check, re-measurement, analysis
- `data/raw/` sweep CSVs, `counters/` (VTune), `run1_interfered/` (rejected run: pitfall evidence), CPU logs, environment;
  `recheck_2026-10-22/` holds an independent re-run
- `data/processed/` aggregated tables
- `plots/` figures used by the report
- `reports/` REPORT, RESULTS_TABLES (generated), HYPOTHESES, AI-USE

## Reproduce (Git Bash; Python with matplotlib; Intel VTune for counters)
```bash
scripts/capture_env.sh > data/raw/env.txt   # hardware / OS / memory / VBS / tools
scripts/build.sh                            # build/ (not committed): {lat,bw,loaded,kern}.exe + asm
scripts/test.sh                             # 366 correctness, regression + sanity checks
scripts/run_sweeps.sh all                   # ~22 min with the machine idle; typeperf CPU log (QUICK=1: 90 s smoke run)
python scripts/check_interference.py        # flag rows measured during other CPU activity
python scripts/remeasure_flagged.py         # re-measure flagged latency-sensitive rows
scripts/run_counters.sh                     # ADMIN Git Bash only: 45 VTune runs (~35 min)
python scripts/analyze.py                   # plots/, data/processed/, reports/RESULTS_TABLES.md
```
