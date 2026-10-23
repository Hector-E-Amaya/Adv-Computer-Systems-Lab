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
- The plots, and the drafts of HYPOTHESES and the REPORT.

The work ran in batches the student approved one at a time. Every batch
passed a correction gate before it was committed: line-by-line review,
regression tests for each bug, an A/B of each bug's effect on the data,
failure-path tests, and a value review of the data. The student made this
gate part of the workflow (decision D4 below).

## Student review contributions
The student's decisions and review concerns, with what checking showed. The
REPORT's pitfall catalog (section 6) cites them as "concern N".

| # | Decision | Effect |
|---|---|---|
| D1 | Skip Intel MLC only if the rubric allows it | the rubric allows "an equivalent" benchmark; own tools written |
| D2 | Use Intel VTune for counters (installed as administrator) | counter evidence on Windows, where `perf` is unavailable |
| D3 | A full project overview before any code | plan, rubric mapping and correctness checks agreed first |
| D4 | "There can't be a single error in any of the scripts or measurements" | the correction gate above became part of every batch |

| # | Concern (student) | Verdict after checking | Outcome |
|---|---|---|---|
| 1 | The four R/W mixes must really be implemented | already implemented | verified: `bw --wpct`, 320 tested combinations |
| 2 | Loaded latency needs a method; the knee is not guaranteed | valid | knee rule fixed before measuring (1.5x idle) |
| 3 | Pinning, repetitions and error bars need explicit procedures | valid | procedures fixed in advance (REPORT section 1) |
| 4 | Results depend on how bytes are counted | valid | useful vs DRAM bytes separated; DRAM model validated by VTune within 2% |
| 5 | The correlation claim depends on the counter | valid, important | `L2_LINES_IN.ALL` added; the demand-miss counter sees only ~54% of DRAM lines |
| 6 | The AMAT 15% target is optimistic | valid | measured hit fractions plus a TLB term |
| 7 | **Cache-set conflicts could invalidate the TLB experiment** | **valid; the AI first dismissed it, wrongly** | page-mode nodes used 64 of 1024 L2 sets, inflating "TLB cost" up to 3x; fixed, with a set-balance check and a failing mutant |
| 8 | Large-page assumptions | valid | 2 MB DTLB reach corrected to 64 MB (32 entries) |
| 9 | Confirm the memory-channel configuration | valid | single channel, by inventory and by measured bandwidth |
| 10 | The smoke script could hide failures (`-e`, pipefail) | valid | explicit per-check PASS/FAIL and a required verdict; tested in 6 scenarios |
| 11 | `rm -rf` deletes results permanently | valid | timestamped result folders; nothing deleted |
| 12 | **Check every script for bugs before continuing** | valid | full review: 7 real bugs fixed with regression tests, one AI finding retracted |
| 13 | An admin run was interrupted by accident | revealed a gap | scripts log INTERRUPTED and exit 130 instead of ending with no verdict |
| 14 | Can other work run during sweeps? | valid: it adds interference | CPU logger plus flag-and-rerun; run 1 confirmed it (30% of rows flagged, latency +13%, bandwidth -12%); archived, quiet re-run |
| 15 | The sequential-chase pitfall holds only if DRAM-resident | valid caveat | confirmed by counters: 1.015 line fills and 0.89 LLC misses per load |
| 16 | TLB steps could be cache conflicts | valid | settled by counters: at 3072 pages both variants ~100% L2, walks 0.85 vs 0 per load |
| 17 | The K-chain plateau does not prove 12 fill buffers | **valid; the AI overclaimed** | worded as an effective concurrency limit (~12), with page walkers as a second candidate |
| 18 | Check definitions before applying Little's Law at the knee | valid, important | VTune run on the top point; the report states the approximation (probe latency as R) |
| 19 | L3 51 GB/s agreeing with Project 1 is not proof | valid | confirmed by counters: 0.86% of line fills at 4 MB miss L3 |
| 20 | The pinning negative result is legitimate | agreed | reported as a negative result |
| 21 | AMAT holds only if test sizes were not used for calibration | valid check; it holds | all 7 test sizes excluded; 64/256 KB noted as near-trivial |
| 22 | The knee at 72-84% is a reasonable departure | agreed | reported in the scorecard |
| 23 | 100-164 outstanding requests vs ~12 per core | fair flag | reconciled: per-core demand misses vs system-wide with prefetches |
| 24 | Correlation 0.96-0.98 needs statistics | **valid, and it changed a conclusion** | permutation p < 0.001, but the LLC-vs-demand difference is not significant; the conclusion now rests on line counts |

## Caught by the AI's own testing and review
- the first bandwidth tool was CPU-bound (10.1 vs 20.8 GB/s; rewritten twice);
- trials re-walked the same subset of the pointer cycle (<= 2% at 512 MB);
- `loaded` placed traffic on the probe core's SMT sibling, and undercounted
  write traffic 2x;
- single-process comparisons measured page-placement luck;
- AMAT fractions were skewed by counter scaling, and TLB cost was counted
  twice in t_L3;
- the knee plot was connected in the wrong order;
- DRAM-latency medians drift 10-25% between runs while minima are stable;
  comparisons use minima.

**AI errors that were caught and retracted:**
- a false "saxpy not vectorized" finding (a symbol-naming mistake);
- the dismissal of concern 7;
- the overclaimed "12 fill buffers";
- broken scripted edits (shell escaping), caught by parsing and redone.

**Rejected data:** run 1's entire sweep dataset (interference-biased), kept
only as pitfall evidence in `data/raw/run1_interfered/`. Flag-and-rerun was
applied only where flags were shown to change values.

The full chronological development log (working notes, the pre-sweep
hypothesis review and the procedures document) is preserved in the
repository's history at the git tag `pre-submission-refactor`.

## Student's own verification

<!-- STUDENT: add, truthfully and in your own words, what you independently
     checked or changed (e.g. reviewing each batch's files and plots before
     approving, running the VTune scripts as administrator, the concerns
     listed above). -->
