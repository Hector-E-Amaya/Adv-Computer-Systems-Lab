# Project #3: SSD Performance Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

**Start here: [reports/REPORT.md](reports/REPORT.md).** All tables:
[reports/RESULTS_TABLES.md](reports/RESULTS_TABLES.md). AI-use note:
[reports/AI-USE.md](reports/AI-USE.md).

**Safety first: [reports/SAFETY.md](reports/SAFETY.md).** Tests touch only
disposable files in `C:\fio-test\`, with direct I/O; never a raw device or
partition.

## Platform
Samsung PM991a 512 GB NVMe (the laptop's system drive, behind Intel RST/VMD,
BitLocker on), Windows 11, fio 3.43 (`windowsaio`, direct I/O). OS disk and
CPU counters from `typeperf` serve as an independent cross-check.

## Layout
- `configs/`        fio job files (one per run, written by the runner)
- `scripts/`        environment capture, safe runner, sweeps, parser, analysis
- `data/raw/`       fio JSON + logs, Windows counters, environment capture
- `data/processed/` parsed tables
- `plots/`          figures
- `reports/`        REPORT, RESULTS_TABLES, SAFETY, HYPOTHESES, NOTES, CONTRIBUTIONS, AI-USE

## Reproduce (Git Bash; fio 3.43; Python with matplotlib)
```bash
scripts/capture_env.sh > data/raw/env.txt   # OS / CPU / SSD / partitions / power / tools
python scripts/fio_run.py --selftest-safety  # unsafe targets must be refused
python -u scripts/run_sweeps.py all          # ~45 min, machine idle (QUICK=1: smoke run)
python scripts/parse_fio.py                  # data/processed/<sweep>.csv
python scripts/analyze.py                    # plots/, summaries, reports/RESULTS_TABLES.md
scripts/diag_admin.sh                        # ADMIN Git Bash only, optional: write cache, BitLocker, SMART
```

## Rubric map (185 points)
| Category | Where |
|---|---|
| Safety and zero-queue baselines (30) | REPORT section 1, `reports/SAFETY.md`, `scripts/fio_run.py` (safety checks), `plots/qd1_percentiles.png`, RESULTS_TABLES 1 |
| Block-size and pattern sweeps (35) | REPORT section 2, `plots/block_size.png`, RESULTS_TABLES 2 |
| Read/write and queue-depth analysis (40) | REPORT section 3, `plots/rw_mix.png`, `plots/qd_knee.png`, RESULTS_TABLES 3-4 |
| Tail latency and device state (25) | REPORT section 4, `plots/tail_latency.png`, `plots/device_state.png`, RESULTS_TABLES 5-6 |
| Reproducibility and data hygiene (25) | REPORT section 5, `configs/`, `data/raw/`, `data/raw/env.txt`, `plots/os_crosscheck.png`, RESULTS_TABLES 8 |
| AI use + pitfall (15) | `reports/AI-USE.md`, REPORT section 6, `plots/pitfalls.png`, RESULTS_TABLES 7, `data/raw/pitfall_run1/` |
| Report quality (15) | `reports/REPORT.md` (limitations: section 8) |
| Pre-experiment hypotheses | `reports/HYPOTHESES.md` (committed first), scorecard in REPORT section 7 |

## Status
- [x] Step 1: skeleton, environment capture, safety setup
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [x] Batch 1: safe runner, parser, sweep script, validation
- [x] Batch 2: sweeps (205 runs, 0 failed, 113.8 GB written) + read-only pitfall re-run
- [x] Batch 3: analysis, plots, OS cross-check
- [x] Batch 4: REPORT.md, AI-USE.md, rubric map
