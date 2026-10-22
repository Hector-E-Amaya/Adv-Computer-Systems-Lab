# Project #3: SSD Performance Profiling
ECSE 4320/6320 Advanced Computer Systems, class-wide project.

**Safety first: [reports/SAFETY.md](reports/SAFETY.md).** Tests touch only
disposable files in `C:\fio-test\`, with direct I/O; never a raw device or
partition.

## Layout
- `configs/`        fio job files
- `scripts/`        environment capture, run, parse, plot scripts
- `data/raw/`       fio JSON output, environment capture
- `data/processed/` parsed tables
- `plots/`          figures
- `reports/`        SAFETY, HYPOTHESES, METHODS, NOTES, REPORT, AI-USE

## Status
- [x] Step 1: skeleton, environment capture, safety setup
- [x] Step 2: pre-experiment hypotheses (`reports/HYPOTHESES.md`)
- [ ] Batch 1: fio job files, run/parse scripts, safety checks, validation
- [ ] Batch 2: sweeps (QD=1, block size, R/W mix, QD knee, tails, device state, pitfall)
- [ ] Batch 3: analysis and plots
- [ ] Batch 4: REPORT.md and AI-USE.md
