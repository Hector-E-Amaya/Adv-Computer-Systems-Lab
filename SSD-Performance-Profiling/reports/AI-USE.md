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
  drafts of SAFETY, HYPOTHESES, NOTES and this REPORT.

The work ran in batches the student approved one at a time. Each batch
passed the correction gate the student made part of the workflow before
it was committed:
- line-by-line review;
- a regression check for each bug;
- tests of the failure paths (the safety self-test, failed-run quarantine);
- a value review of the data.

Write volume and timing were set with the student's constraints in mind:
system SSD, minimal wear, an idle machine during sweeps.

## What was independently checked, corrected or rejected
Full chronological record: [NOTES.md](NOTES.md). Attribution:
[CONTRIBUTIONS.md](CONTRIBUTIONS.md) (items 25-32).

**Raised by the student, and verified:**
- **"BitLocker may not be the primary bottleneck" (25).** The AI had blamed
  software encryption without evidence. A read-only scaling test showed
  flat throughput at 8-14% CPU, and the claim was **retracted**. The ceiling
  is device/link-side, cause unverified.
- **Investigate the 200 ms writes without adding wear (26).** Existing data
  alone showed the 200 ms is p99 (not a lone outlier), QD=1 writes are fast,
  and the tails are queueing behind a sustained write cap. Write volume was
  cut (16 GiB files, 5 s write runs, 120 s steady state).
- **Like-for-like pitfall (27).** The original comparison mixed I/O
  engines. Cold vs warm with the same engine (24x) proved the page cache is
  the cause, and corrected an AI note that windowsaio buffered reads miss
  the cache.
- **Wording held to what the data prove (28, 32):** "write buffer", not
  "SLC cache"; the burst is "not observable through the cap", not "no SLC
  cache".
- **Ceilings beyond normal limits (29).** Confirmed that writes are not
  CPU-bound either. The candidate causes are reported as hypotheses.
- **84 K vs 126 K IOPS (30)** and **whether the 272x write tail repeats
  (31):** checked; different QD, and yes (195x / 272x / 272x).

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
- MiB/s were reported as MB/s in NOTES;
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

## Student's own verification

<!-- STUDENT: add, truthfully and in your own words, what you independently
     checked or changed (e.g. installing fio, running the admin diagnostic,
     keeping the machine idle during sweeps, reviewing each batch's files and
     plots, and the concerns listed in CONTRIBUTIONS.md). -->
