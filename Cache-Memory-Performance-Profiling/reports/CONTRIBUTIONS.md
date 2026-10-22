# Student review contributions (factual log)

This records the concerns and decisions the student raised while working with
the AI assistant, and what each one led to. It feeds the "independently
modified, tested, or rejected" part of AI-USE.md and the report. Items found
by the assistant's own testing are listed separately at the end, so credit is
attributed accurately. Commit hashes point to the evidence.

## Decisions
| Date | Decision | Effect |
|---|---|---|
| 2026-10-21 | Skip Intel MLC only if the rubric allows it | Confirmed: the rubric says "MLC **or an equivalent** benchmark"; own tools written |
| 2026-10-21 | Use Intel VTune for counters (option B), installed as administrator | Counter evidence available on Windows (no `perf`) |
| 2026-10-21 | Required a full overview of the project before Step 2 | Plan, rubric mapping and correctness checks agreed before any code |
| 2026-10-21 | Made the correction process part of the workflow: "there can't be a single error in any of the scripts or measurements" | Every batch now passes a review gate (review, regression test per bug, A/B impact, failure-path tests, NOTES log) |

## Concerns raised and outcomes
| # | Concern (student) | Verdict after checking | Outcome / evidence |
|---|---|---|---|
| 1 | R/W mixes 100/0, 0/100, 70/30, 50/50 must actually be implemented | Already implemented | Verified: `bw --wpct`, 320 tested combinations |
| 2 | Loaded latency needs a clear method; knee location not guaranteed | Valid | Objective knee rule fixed before measuring (1.5x idle), METHODS.md (c63e0a7) |
| 3 | Pinning / repetitions / error bars need explicit procedures | Valid | METHODS.md (c63e0a7) |
| 4 | H2: results depend on how bytes and requests are counted | Valid | Useful vs DRAM bytes separated; DRAM model later validated by VTune within 2% (f1e1930) |
| 5 | H4: correlation claim too strong; counter choice matters | Valid, important | Added `L2_LINES_IN.ALL`. VTune showed the demand-miss counter sees only ~54% of DRAM lines, while L2_LINES_IN matched expected lines within 1.2% (f1e1930) |
| 6 | H5: AMAT 15% target too optimistic | Valid | Measured hit fractions + TLB term (HYPOTHESES_REVIEW.md) |
| 7 | **H6: cache-set conflicts could invalidate the TLB experiment** | **Valid. The assistant first dismissed it, wrongly** | Real bug found with VTune: page-mode nodes used 64 of 1024 L2 sets, inflating "TLB cost" up to 3x (8192 pages: 26.7-44.5 ns vs 14.6 ns fixed). Fixed, with a balance check and a failing mutant (f1e1930) |
| 8 | H6: large-page assumptions | Valid | 2 MB DTLB reach corrected to 64 MB (32 entries) (c63e0a7) |
| 9 | Confirm the memory channel configuration | Valid | Single channel confirmed by inventory and measured bandwidth (c63e0a7) |
| 10 | Smoke script lacked `-e`, so failures could go unnoticed; `head` with pipefail | Valid | Explicit per-check PASS/FAIL, nonzero exit, verdict required; tested in 6 scenarios with a stand-in (36f91e5) |
| 11 | `rm -rf` permanently deletes results | Valid | Replaced by timestamped result folders; nothing deleted (36f91e5) |
| 12 | **Every script must be checked for bugs before continuing**, since bugs could skew results | Valid | Full review: 7 real bugs fixed with regression tests, one assistant finding retracted (4876bfb); line-ending and editing issues fixed (2a9339c) |
| 13 | Accidental interruption of the admin run | Revealed a gap | Script now logs INTERRUPTED and exits 130 instead of ending with no verdict; tested (6a9e324) |
| 14 | Can other work (Edge, slides/docs) run during sweeps? | Valid: it adds memory/CPU interference | Implemented: typeperf per-CPU logger, per-row timestamps, flag-and-rerun rule (Batch 2). **Run 1 confirmed the concern:** 30% of rows flagged, with median latency +13% and bandwidth -12% on flagged reps. Archived as pitfall evidence; quiet re-run |

## Found by the assistant's own testing (for accurate attribution)
- `bw` first version was CPU-bound (10 GB/s vs 20.8 GB/s expected); rewritten twice (352e811).
- DRAM-latency run-to-run noise: medians drift 10-25%, minima stable (352e811).
- Topology test bug, `bc` missing, Windows exe-path issue in tests.
- Review-pass bugs: lat trial restart (<=2% effect at 512 MB, measured), bw main-thread spin, mean/median mismatch, --smt ordering, R/W mix 30.5%, partial-line NT model, latent topology guards (4876bfb).
- The assistant's own false finding ("saxpy not vectorized at -O2") caught and retracted (4876bfb).
- VTune values checked against first principles (0.06% instructions, 1.2% lines), which exposed the page-mode bug behind concern 7 (f1e1930).

The student should describe these contributions in their own words in the
report; this log is the factual record behind them.
