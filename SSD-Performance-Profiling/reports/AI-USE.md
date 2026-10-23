# AI-use note

## Tools
- **Claude Code** (Anthropic, Claude Opus 5.5) in the Claude desktop app,
  with access to this repository and a shell. It wrote the scripts, ran the
  fio sweeps and analysis, and committed after each verified batch.
- **fio 3.43** (official Windows build), installed by the student.
- Python 3.14 with matplotlib, git, Windows `typeperf`. Administrator queries
  (`scripts/diag_admin.sh`: write cache, BitLocker, SMART) were run by the
  student from an elevated shell.

## What the AI generated
- All scripts (`scripts/`): environment capture, the safety-checking runner
  `fio_run.py`, `run_sweeps.py`, `parse_fio.py`, `analyze.py`,
  `diag_admin.sh`.
- Every fio job file (`configs/`, written by the runner), the plots, and the
  drafts of SAFETY, HYPOTHESES and the REPORT.

The work ran in batches the student approved one at a time. Each batch
passed the correction gate the student made part of the workflow before
it was committed:
- line-by-line review;
- a regression check for each bug;
- tests of the failure paths (the safety self-test, failed-run quarantine);
- a value review of the data.

Write volume and timing were set with the student's constraints in mind:
system SSD, minimal wear, an idle machine during sweeps.

## Student review contributions
The student's constraints and review concerns, with what checking showed.
Numbering continues from Project 2 (concerns 1-24). The REPORT's pitfall
catalog (section 6) cites them as "concern N".

**Constraints set by the student:** never benchmark a partition with
valuable data (disposable files only, `reports/SAFETY.md`); do not raise
write volume aggressively on the system SSD (wear policy); keep the machine
idle during sweeps.

| # | Concern (student) | Verdict after checking | Outcome |
|---|---|---|---|
| 25 | "BitLocker may not be the primary bottleneck" | **valid; the AI's claim was unsupported** | read-only scaling test: throughput flat at ~620-770 MB/s from 1 to 4 threads at 8-14% CPU, so not CPU/encryption-bound; claim retracted |
| 26 | Are the 200 ms writes outliers or tail percentiles? Investigate without adding wear | valid | from existing data only: 200 ms is p99 for large writes; QD=1 writes are fast; tails are queueing behind a sustained write cap; write volume reduced |
| 27 | The buffered pitfall must compare like for like | valid: the engines differed | same file, block size, QD and engine, cold vs warm: 10.7 K vs 189 K IOPS (the cache, not the engine) |
| 28 | Faster QD=1 writes don't prove an SLC cache | valid | reported as "write buffer / device write cache", not "SLC" |
| 29 | Flat ~0.7 GB/s read and ~0.1 GB/s write ceilings suggest another bottleneck | valid | writes are not CPU-bound either (CPU median 10%); causes reported as hypotheses |
| 30 | 84 K mix read IOPS vs 126 K peak: same conditions? | different QD | the mix is QD 16 (QD sweep at 16: 82.5-83.0 K); 126 K is QD 32 |
| 31 | Does the 272x write tail repeat across trials? | yes | p99.9/p50 195x / 272x / 272x per repetition |
| 32 | No initial write burst doesn't prove the SLC cache isn't working | valid | reported as "not observable through the ~140 MB/s cap" |

**Caught by the AI's own testing and review** (examples):
- one fio thread cannot fill a deep queue (effective QD 20 at iodepth 32),
  so the sweeps were redesigned around 4 jobs and report Little's-Law QD;
- a failed fio run's error text was parsed as data (now quarantined as
  `*.json.failed`);
- an escaped ':' in log paths silently disabled the bandwidth log;
- the buffered pitfall did not appear until `invalidate=0` and a warming
  pass were added;
- Windows command output with NUL characters made `env.txt` binary;
- a missing `ramp_time` column broke the analysis;
- MiB/s were reported as MB/s in the working notes;
- a mislabelled table column and stale docstrings / plot titles that
  still said "SLC" were fixed.

**AI errors that were caught and retracted or corrected:**
- the BitLocker bottleneck claim (above);
- a wear estimate of "60-80 GB" (correct upper estimate ~163 GB; actual
  113.8 GB in the sweeps, ~128 GB including validation, smoke tests and
  re-runs; "113.8 GB total" was itself corrected in the final audit);
- a latent gap in the safety check (fio's `directory` / `opendir` /
  `filename_format` options were not refused; never used, fixed in the
  final audit with regression cases);
- shell-escaping mistakes in test commands (caught by fio errors, redone
  through Python).

**Rejected data:** pitfall run 1 (one repetition disturbed by activity
during the run, another lost its cache) is archived in
`data/raw/pitfall_run1/` as evidence and replaced by a quiet read-only
re-run.

The full chronological development log (working notes) is preserved in the
repository's history at the git tag `pre-submission-refactor`.

## Student's own verification

<!-- STUDENT: add, truthfully and in your own words, what you independently
     checked or changed (e.g. installing fio, running the admin diagnostic,
     keeping the machine idle during sweeps, reviewing each batch's files and
     plots, and the concerns listed above). -->
