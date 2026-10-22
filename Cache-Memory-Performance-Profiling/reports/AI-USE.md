# AI-use note

## Tools
- **Claude Code** (Anthropic, Claude Opus 5.5) in the Claude desktop app,
  with access to this repository and a shell: it wrote code, built and ran
  the tools and scripts, and committed after each verified batch.
- **Intel VTune Profiler 2026.6:** hardware counters. It was installed and
  run (as administrator) by the student.
- Locally installed gcc 16.2 / binutils (MSYS2 UCRT64), Python 3.14 with
  matplotlib, git, Windows `typeperf`.

## What the AI generated
- All source (`src/`): `lat`, `bw`, `loaded`, `kern`, `common`.
- All scripts (`scripts/`): build, test, sweeps, counter runs, interference
  flagging, re-measurement, analysis.
- The plots, and the drafts of HYPOTHESES, HYPOTHESES_REVIEW, METHODS, NOTES
  and this REPORT.

The work ran in batches the student approved one at a time. Every batch
passed a correction gate before it was committed: line-by-line review,
regression tests for each bug, an A/B of each bug's effect on the data,
failure-path tests, and a value review of the data. The student made this
gate part of the workflow (CONTRIBUTIONS.md).

## What was independently checked, corrected or rejected
Full chronological record: [NOTES.md](NOTES.md). Attribution:
[CONTRIBUTIONS.md](CONTRIBUTIONS.md).

**Raised by the student, and verified:**
- **H6 cache-set conflicts.** The AI first dismissed this concern. VTune
  later showed it was real: the page layout used 64 of 1024 L2 sets and
  inflated TLB cost up to 3x. Fixed before any sweep.
- **H4 counter choice.** Added `L2_LINES_IN.ALL`: the demand-miss counter
  sees only ~54% of DRAM lines.
- **Objective knee rule** fixed in advance; **byte-counting models**
  validated by VTune.
- **Script robustness:** explicit PASS/FAIL and no deletions in VTune
  scripts; **a full code review before measuring** (7 real bugs found).
- **Concurrent work during sweeps.** A CPU logger was added, and run 1 was
  rejected for interference.
- **Evidence requested for claims that only "agreed":** the sequential
  pitfall is DRAM-resident, the TLB steps are walks, and 51 GB/s is served by
  L3. All three were confirmed by counters. The "12 fill buffers" wording
  was corrected.

**Caught by the AI's own testing and review** (examples):
- the bandwidth tool was CPU-bound (rewritten twice);
- trials re-walked the same subset of the pointer cycle;
- `loaded` placed traffic on the probe core's SMT sibling, and undercounted
  write traffic 2x;
- single-process comparisons measured placement luck;
- AMAT fractions were skewed by counter scaling, and TLB cost was counted
  twice in t_L3;
- the knee plot was connected in the wrong order.

**AI errors that were caught and retracted:**
- a false "saxpy not vectorized" finding (a symbol-naming mistake);
- the H6 dismissal above;
- the overclaimed "12 fill buffers";
- broken scripted edits (shell escaping), caught by parsing and redone.

**Rejected:** run 1's entire sweep dataset (interference-biased), kept only
as pitfall evidence; flag-and-rerun applied only where flags were shown to
change values.

## Student's own verification

<!-- STUDENT: add, truthfully and in your own words, what you independently
     checked or changed (e.g. reviewing each batch's files and plots before
     approving, running the VTune scripts as administrator, the concerns
     listed in CONTRIBUTIONS.md). -->
