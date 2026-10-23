# Project #3: SSD Performance Profiling (submission)
ECSE 4320/6320 Advanced Computer Systems, class-wide project (185 points).

fio study of a Samsung PM991a 512 GB NVMe SSD (the laptop's system drive,
behind Intel RST/VMD, BitLocker on), Windows 11, fio 3.43 (`windowsaio`,
direct I/O). Windows disk/CPU counters (`typeperf`) serve as an independent
cross-check.

**Report: [reports/REPORT.md](reports/REPORT.md)**

**Data-loss safety: [reports/SAFETY.md](reports/SAFETY.md).** Tests touch
only disposable files in `C:\fio-test\` with direct I/O; never a raw device
or partition. Every run passes `scripts/fio_run.py`'s safety check.

## Deliverables
| Deliverable (project overview) | Where |
|---|---|
| FIO configs, scripts, raw results, parsed data, plotting code | `configs/` (one job file per run), `scripts/`, `data/raw/` (fio JSON + logs), `data/processed/`, `scripts/analyze.py` |
| Methodology: SSD model, interface, filesystem vs raw, direct I/O, capacity/range, OS, CPU, temperature | REPORT section 1; `data/raw/env.txt`, `data/raw/diag_admin.txt` |
| Plots/tables with units, error bars, fixed vs varied knobs | `plots/` (min..max over 3 repetitions), [reports/RESULTS_TABLES.md](reports/RESULTS_TABLES.md) |
| Analysis: queueing, controller behavior, interface ceilings, tail implications | REPORT sections 2-4 |
| AI-use note | [reports/AI-USE.md](reports/AI-USE.md) (includes the student's review contributions) |
| Pre-experiment hypothesis | [reports/HYPOTHESES.md](reports/HYPOTHESES.md) (committed before fio was installed); scored in REPORT section 7 |
| Correctness / sanity validation | safety self-test (`python scripts/fio_run.py --selftest-safety`), Little's Law vs requested QD, Windows counters vs fio (`plots/os_crosscheck.png`); REPORT sections 3b and 5 |
| Negative / misleading case | REPORT section 6 (featured pitfalls plus a catalog of all cases) |
| Honest anomalies | REPORT sections 2, 4 and 8 |

## Required experiments
| Experiment | Plot / evidence | Report |
|---|---|---|
| Zero-queue baselines: QD=1, 4 KiB random R/W and 128 KiB sequential R/W, with p95/p99 | `plots/qd1_percentiles.png`, RESULTS_TABLES 1 | section 1 |
| Block-size sweep (throughput and latency from the same runs, random and sequential) | `plots/block_size.png`, RESULTS_TABLES 2 | section 2 |
| Read/write mix sweep (100/70/50/0% reads, fixed 4 KiB random) | `plots/rw_mix.png`, RESULTS_TABLES 3 | section 3a |
| Queue-depth sweep: throughput vs latency, knee, Little's Law | `plots/qd_knee.png`, RESULTS_TABLES 4 | section 3b |
| Tail latency at mid-QD and near-knee QD | `plots/tail_latency.png`, RESULTS_TABLES 5 | section 4a |
| Working-set/LBA-range and device-state note | `plots/device_state.png`, RESULTS_TABLES 6 | section 4b |
| Misleading setup | `plots/pitfalls.png`, RESULTS_TABLES 7 | section 6 |

## Rubric map (185 points)
| Category | Where |
|---|---|
| Safety and zero-queue baselines (30) | REPORT section 1, `reports/SAFETY.md`, `scripts/fio_run.py` |
| Block-size and pattern sweeps (35) | REPORT section 2 |
| Read/write and queue-depth analysis (40) | REPORT section 3 |
| Tail latency and device state (25) | REPORT section 4 |
| Reproducibility and data hygiene (25) | REPORT section 5, `configs/`, `data/raw/`, `plots/os_crosscheck.png` |
| AI use + pitfall (15) | `reports/AI-USE.md`, REPORT section 6 |
| Report quality (15) | `reports/REPORT.md` (limitations: section 8) |

## Layout
- `configs/` fio job files, one per run, written by the runner
- `scripts/` environment capture, safe runner, sweeps, parser, analysis, admin diagnostic
- `data/raw/` fio JSON per sweep, state logs, Windows counters, environment.
  Beyond the sweeps: `investigate/` and `validation/` (Batch 1 checks behind
  the read-scaling and pitfall evidence), `pitfall_run1/` (an archived,
  disturbed run), `recheck_2026-10-22/` and `recheck_ceiling/` (re-runs)
- `data/processed/` parsed CSVs and summaries
- `plots/` figures used by the report
- `reports/` REPORT, RESULTS_TABLES (generated), SAFETY, HYPOTHESES, AI-USE

## Reproduce (Git Bash; fio 3.43; Python with matplotlib)
```bash
scripts/capture_env.sh > data/raw/env.txt   # OS / CPU / SSD / partitions / power / tools
python scripts/fio_run.py --selftest-safety  # all 10 unsafe cases must be refused
python -u scripts/run_sweeps.py all          # ~45 min, machine idle (QUICK=1: smoke run)
python scripts/parse_fio.py                  # data/processed/<sweep>.csv
python scripts/analyze.py                    # plots/, summaries, reports/RESULTS_TABLES.md
scripts/diag_admin.sh                        # ADMIN Git Bash only, optional: write cache, BitLocker, SMART
```
