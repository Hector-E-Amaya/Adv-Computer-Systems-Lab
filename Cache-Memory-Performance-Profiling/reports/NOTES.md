# Working notes (findings to carry into REPORT.md)

## Batch 1 (tools + correctness)
- **Topology:** 4 physical cores, SMT siblings adjacent (0,1) (2,3) (4,5)
  (6,7). Single-thread tools pin to CPU 2 (core 1). Traffic threads use one
  logical CPU per other core, with core 0 last (OS interrupts).
- **First latency readings** (`lat`, random chase) match H1:
  - 16 KB: 5.1 cycles (1.40 ns);
  - 256 KB: 14.1 cycles;
  - 4 MB: 64.7 cycles (17.6 ns);
  - 256 MB: ~106-115 ns (min).
- **bw first version was CPU-bound, caught before any sweep:** 10.1 GB/s
  single-thread sequential read vs 20.8 GB/s from Project 1. In L1 the loop
  managed only 238 M lines/s (~15.5 cycles/line). Two rewrites:
  1. pattern-specialized loops, power-of-two masks, R/W mix in groups of 10;
  2. one function per granularity with 256-bit accumulators.
  Result: L1 1.5 G lines/s (~2.4 cycles/line), so DRAM numbers are now
  memory-bound. Sequential 1 thread 17.4 GB/s, 4 threads 22.6 GB/s. Random 1
  thread 4.1 GB/s (not CPU-bound: 640 M lines/s in L1). Single-thread
  sequential stays below Project 1's 20.8 GB/s dedicated 512-bit read loop:
  report the difference.
- **Writes:** 100% regular stores give 7.4 GB/s useful = 14.6 GB/s DRAM
  traffic (write-allocate). Non-temporal stores give 16.4 GB/s.
- **loaded first look:** idle 128 ns -> 172 ns at 12.5 GB/s -> 333 ns at
  20.7 GB/s (3 traffic threads), i.e. 2.6x idle near saturation.
- **K chains (MLP):** with min latencies, K=8 gives ~6.9-7.0x the K=1
  throughput, and per-chain latency rises ~15% (106 -> 122 ns). Physical
  max is 8x.
- **asm:**
  - chase1 is a dependent `movq (%rax),%rax` chain;
  - chase12 keeps 12 chains in registers;
  - **chase14/16 spill 2-3 chains to the stack** (~+5 cycles store-forward on
    those chains, <2% at DRAM latency). Use K>12 only for DRAM.

### Measurement-noise finding (affects how results are summarized)
- DRAM-latency **medians drift 10-25% between runs** (e.g. 107-131 ns in six
  back-to-back `lat` runs). Per-trial maxima reach ~179 ns. The **minimum**
  is stable to ~1% within a session (105.6-106.7 ns).
- Interference only adds time. Cross-run comparisons in tests therefore use
  minima. Sweeps will report median + min + max, and the analysis will
  discuss both.
- A heavy 4-thread bandwidth run just before an idle-latency measurement
  raised it ~7% (114 vs 106 ns). The likely cause is uncore/ring clock state
  after load. Tests now measure idle latency back-to-back with `lat`. In the
  sweeps: interleave, and do not run idle latency right after heavy traffic
  without a pause.
- The DRAM minimum also drifted between sessions: 106 ns earlier,
  111-113 ns later the same day.

### Pre-sweep code review (every source file and script, before Batch 2)
Real bugs, all fixed, each with a regression test in `scripts/test.sh`:
- **B5 lat: trials restarted from the same node.** A trial shorter than one
  lap re-walked the same subset (e.g. ~450K of 8M nodes at 512 MB), and the L3
  could retain part of it. Fix: chase positions persist across calls. Test:
  two s-step calls == one 2s-step walk. **Measured effect** (old vs fixed, back
  to back, 3 runs each): 256 MB within noise; 512 MB K=1 old minimum 1-2%
  lower (106.9-108.5 vs 109.2-109.8 ns). Small but in the predicted
  direction.
- **B1 bw:** the main thread, pinned to worker 0's CPU, busy-spun while
  workers started, delaying worker 0. Fix: SwitchToThread. Test: wall time
  bounded.
- **B2 bw:** line rate and DRAM estimate used the mean of trials, useful GB/s
  the median. Fix: all from the median trial. Test: mlines x gran == useful.
- **B3 bw --smt:** CPU order 2,3,4,... put 2 threads on one physical core.
  Fix: one per core, then siblings. Test: 2 threads -> "2 4"; 5 -> "2 4 6 0 3".
- **B7 loaded:** 256-line blocks made 30% writes really 30.5%. Fix: 250-line
  blocks. The delay count is now a local, so its cost no longer includes a
  reload. Test: delay 0 vs 400 traffic ratio.
- **B8 bw:** partial-line non-temporal stores break the DRAM-traffic model.
  Fix: --nt needs --gran 64 for measurements (allowed in --check).
- **Latent:** a topology read failure would divide by zero, and >32 cores
  would overflow cpus[64] (bw, loaded). Fix: guards. Also `usage()` marked
  noreturn; the build is warning-free.
- vtune_smoke.sh: git "dubious ownership" noise under admin, now quiet with a
  fallback.

**My own false finding, caught:** "B4: kern saxpy not vectorized at -O2" was
wrong. gcc names the specialised copy `saxpy.constprop.0`, and my
exact-label search for `saxpy:` found nothing. With correct matching, -O2
vectorizes both kernels (`vfmadd213ps`/`vmulps` on ymm; -fopt-info confirms
lines 23 and 30). The -O3 "fix" was reverted. The asm test now matches any
specialised suffix and checks both kernels.

Test-script bugs caught by running the suite (not measurement bugs): `bc`
not installed (timing now via Python), and Windows Python needing an
absolute exe path.

Tooling/line-ending issues (no measurement impact):
- Python `write_text` and pandas `to_csv` write CRLF on Windows. Some working
  copies (test.sh, vtune_smoke.sh, Project 1 processed CSVs) had CRLF; the
  committed copies were LF. All converted, and git confirms content-identical.
- Root cause fixed in Project 1's analyze.py / verify_vec.py (explicit `\n`).
  Re-running them reproduced every committed plot and CSV byte-for-byte.
- An attempted scripted fix inserted real newlines into string literals (a
  shell-escaping problem in how the assistant passed the command). The syntax
  error was caught by parsing, the file restored, and the edit redone with
  the file-edit tool. That attempt also showed a pattern matching 7 places
  instead of 6, one of which was a `to_string` call it would have broken:
  edits now use exact, unique patterns. Exit codes are checked explicitly.

### VTune smoke test, first attempt (2026-10-21 20:20, interrupted)
- Run as administrator: "Collection started ... Collection stopped" for the
  saxpy event set, with no driver/permission error. So **hardware event
  sampling works with VBS/memory integrity on**, when elevated.
- The run was stopped by accident during VTune's "Finalizing results" step
  and left a log with no verdict. Gap fixed: the script now traps
  Ctrl+C/close and logs "INTERRUPTED ... run it again" (exit 130), and warns
  up front that finalizing takes minutes. Tested with a hanging stand-in
  interrupted like Ctrl+C. The stand-in's output was removed afterwards.

### VTune smoke test, completed (2026-10-21 20:24): ALL CHECKS PASSED
Counter values were then validated against first principles. A PASS only
means the report has numbers.
- **SAXPY** (16M elements x 271 passes = 4.547e9 elements, DRAM):
  - per-function INST_RETIRED in the saxpy loop = 3,412,005,118 vs
    0.75/element x 4.547e9 = 3,409,900,000 (**0.06%**; loop = 6 instructions
    per 8 floats);
  - measure_ghz = 300,000,450 vs 2x3x5M x 10 = 300,000,000 (exact);
  - L2_LINES_IN.ALL in saxpy = 574.8M vs 271 x 2,097,152 lines = 568.3M
    (+1.2%), so it counts DRAM line traffic;
  - MEM_LOAD_RETIRED.L3_MISS = 307M = only ~54% of lines (prefetch hides the
    rest), the H4 caveat confirmed;
  - DRAM: VTune 17.1 GB/s average (19.3 max) vs bw 16.8 GB/s, within 2%;
    validates bw's accounting for reads.
- **Sampling precision:** event counts are samples x sample-after value
  (e.g. DTLB walks in saxpy = 4 samples x 100003), so each count is exact
  only to about +-1 SAV. Rare events need a lower SAV in run_counters.sh.
- **Page mode (8192 pages):** ~0.95 DTLB walks per load (beyond STLB reach),
  as expected. **But 25% of loads went to DRAM and latency was 43 ns for a
  512 KB data footprint: a real bug.**
  - The (7*i mod 64) slot and the frame bits used only 64 of 1024 L2 sets:
    the user's H6 "cache-set conflicts" concern, which I had wrongly
    dismissed.
  - A random-hash slot fixed L2 but unbalanced L1 slightly (512 pages
    3.55 vs 3.29 ns), so it was replaced by the exact formula
    (i + 7*(i/64)) mod 64.
  - Old vs fixed, back to back: 256/512 pages equal (3.27); 8192: 26.7 ->
    14.6 ns; 32768: 41.6 -> 27.6 ns. The old layout's error also varied
    between runs (26.7 vs 44.5 ns at 8192 pages), since it depended on
    physical page placement: irreproducible, not just wrong.
  - Regression: `lat --check` verifies the set balance. The old layout is
    built as `lat_slotmutant` and must fail.
- **Debug symbols:** `-g` added so VTune shows function names. objdump
  confirms identical machine code with and without -g for all 5 objects
  (8839 instructions).

### Batch 2 scripts: correction gate (before any full sweep)
- **loaded --smt** put the first sibling traffic thread on CPU 3, the chase
  core's own SMT sibling: core-resource contention, not memory queueing.
  The chase sibling is now last (used only by a 7th thread; sweeps stop at
  6). Regression test: 6 threads -> "4 6 0 5 7 1".
- **loaded undercounted DRAM traffic for write mixes.** Regular stores move
  each line twice (RFO + write-back). 3 threads at 100% writes: 7.3 GB/s of
  lines = **14.7 GB/s DRAM**, at 508 ns. A new est_dram_gbps_med column uses
  the bw model, validated by VTune within 2%. Regression tests: equal to
  total at 0% writes; ~2x traffic + chase at 100%.
- **bw is loop-bound in cache:** 16 KB and 1 MB both read ~83 GB/s (loop cap
  ~1.3 G lines/s). bwws points below L3 are **lower bounds, not cache
  bandwidth**. True L1/L2/L3 read bandwidth is taken from Project 1's
  peak.c on the same laptop (429 / 157 / 51 GB/s). DRAM points unaffected.
- **Quick sweep values reviewed:**
  - L1 5.2 cycles; 1 MB 22 cycles (L2 + STLB);
  - sequential-chase pitfall: 4.6 ns vs 120 ns random (26x under-report);
  - TLB at 8192 pages +10 ns over control;
  - knee 119 -> 295 ns at 21.7 GB/s;
  - gran 8 vs 64: same line rate (H2).
- **Per-process DRAM-latency variation (real finding).** Each run is a new
  process with different physical pages. The idle `loaded` minimum ranged
  112-137 ns across separate runs; min-over-trials is stable only within a
  process. A one-process-each test check failed (1.13, 1.20) for this
  reason, not because of preceding traffic (disproved: pairs right after
  100%-write traffic agreed within 0.5%). Fixed the **estimator**, not the
  threshold: 3 interleaved runs each, lowest minima, +-5% kept (now 1.031,
  0.994). The sweeps' 3 repetitions are separate processes, so the analysis
  reports spread across processes.
- **run_counters.sh:**
  - The timed function is located by address via `nm` (VTune showed
    func@0x... in the smoke test); the name is accepted if VTune has it.
  - Regexes are anchored (`^mul$` must not match `__muldc3`) and passed via
    ENVIRON (awk -v turned `\.` into `.`).
  - A leftover dead line was removed.
  - Tested with a stand-in in 6 scenarios (addr / named PASS; wrong
    function, missing event, failed collection FAIL; Ctrl+C ->
    INTERRUPTED). Stand-in output was verified fake (36/36) and removed.
- **CPU logger** (typeperf, per CPU, 1 s) runs during sweeps. Each row carries
  t_start/t_end so disturbed points can be flagged. The first typeperf
  sample is blank: the analysis skips it.

### Full sweep run 1 (2026-10-22 00:58-01:20): rejected for interference, kept as pitfall evidence
- Completed: 936 rows (ws 210, tlb 108, mlp 30, pin 30, bwws 96, bw 264,
  knee 198), 0 invalid, exit 0. Large pages skipped (privilege not granted).
- `scripts/check_interference.py` (the pre-declared METHODS rule: a non-benchmark
  CPU > 25% busy during the row) flagged **283/936 rows (30%)**.
- **The flags are meaningful.** Flagged vs unflagged repetitions of the same
  configuration (median ratio over configurations):
  - ws latency: min +6.4%, median **+12.8%** (p90 +46%);
  - mlp latency: +15-18%;
  - bwws bandwidth: **-12%**; bw bandwidth: -3.3%;
  - knee: latency +3.9%, DRAM GB/s -3.8%.
  The effect always goes the direction interference predicts, and exceeds the
  normal rep-to-rep spread (e.g. bwws 2.5%).
- **Source: bursts, not the setup.** Minutes 01:06, 01:07 and 01:10 had zero
  flags. Bursts line up with launching the run, the two status checks the
  student requested (01:02-01:03, 01:14-01:15; Claude app rendering replies
  plus the checks themselves) and other activity (likely light document
  editing).
- Decision: archive as `data/raw/run1_interfered/` (benchmark-pitfall
  evidence: background activity measurably biases memory benchmarks, and is
  detected by a CPU logger). Then re-run the full sweeps with the machine
  quiet (no Edge, no status checks), re-flag, and re-measure only rows still
  flagged.
- `check_interference.py --dir data/raw/run1_interfered` reproduces the
  flagging byte-for-byte.

### Full sweep run 2 (quiet, 2026-10-22 01:42-02:04): the dataset
- 936 rows, 0 invalid. The machine was quiet (Edge closed, student away, no
  status checks): the median non-benchmark busy level dropped from 17.7% to
  10.4%, and flags fell from 283 to 103.
- **Which flags mattered (flagged vs unflagged reps of the same config):**
  - single-thread latency rows were still biased (ws median +2.7%, tlb
    +7.8%);
  - rows using several CPUs showed no effect (bw 4 threads 0.997; knee
    0.995-1.003). With 7 of 8 CPUs busy, Windows' own work must land on the
    remaining CPU. Flags there are expected and harmless;
  - unpinned `pin` rows are flagged by design (the thread migrates).
- **Re-measured:** the 26 latency-sensitive flagged rows (13 ws, 6 tlb, 4
  pinned pin, 3 single-thread bw) with `scripts/remeasure_flagged.py`.
  - Exact command rebuilt from each row (dry-run checked against the sweep
    definitions); own typeperf log per round; same flag rule
    (`check_interference.worst_other`, shared).
  - 25 clean in round 1, 1 in round 2.
  - Originals kept in `data/raw/remeasured_originals.csv`.
  - Independent re-check: 103 - 26 = **77 flagged remain, all multi-CPU or
    unpinned**.
- Gate-review fixes in the re-measure tooling: retry-on-flag was not
  actually implemented in the first draft (only retry-on-failure), and the
  flagger only read the sweep's CPU log. Both fixed (rounds; merged logs) and
  verified: refactored flagger output byte-identical on runs 1 and 2.
- **Value review** (vs hypotheses and known values):
  - **latency:** L1 5.1 cyc, L2 14.1, 1 MB 19.6 (STLB), L3 4 MB 52 cyc,
    DRAM 105 (64 MB) to 116 ns (512 MB); spread across 3 processes mostly
    <2%. Sequential chase at DRAM 4.4 ns (26x pitfall);
  - **TLB:** +1.9 ns step right after 64 pages (~7 cyc STLB hit, as H6
    predicted) and +9.4-10 ns after 2048 pages (~37 cyc walk, within H6's
    20-40); grows to +30 ns at 65536 pages;
  - **MLP:** linear to K=12 (5.68 GB/s), flat at 14-16 with per-chain
    latency 150-172 ns (fill-buffer limit, H3);
  - **knee:** flat ~114-125 ns until ~17-20 GB/s, then 282-451 ns at
    22-23 GB/s. **R/W mix** at 3 threads: w0 22.6 GB/s / 282 ns, w30 19.2 /
    392, w50 18.4 / 436, w100 18.7 / 497 (DRAM GB/s);
  - **bw:** seq read 19.5 (1 thread) / 23.1 GB/s (4 threads). 100% regular
    writes 9.0 useful = 18.0 DRAM; NT writes 22.1. Granularity 8 vs 64 B:
    277 vs 305 M lines/s (line-limited);
  - **bwws:** L3 reads 51-52 GB/s, matching Project 1's peak.c (51 GB/s)
    independently. L1/L2 points (95-102) are loop-bound, as documented.
  - **pin** (honest negative): on the quiet machine, unpinned was NOT
    noisier (CV 0.7% vs pinned 4.6%, which includes one 131.8 ns
    outlier). H7's "unpinned -> noisier" is not supported here. Run 1's
    interference is the stronger pitfall evidence.

### Counter run extended before running (student review of run 2)
- 36 -> 45 runs:
  - `lat_seq_256M` (pitfall is DRAM-resident);
  - `bw_seq_4M` / `bw_seq_256M` (51 GB/s served by L3; DRAM reference);
  - page/control pairs at 96 and 3072 pages (just past the steps);
  - two VTune memory-access runs: 100% regular writes (validates the RFO
    model; the smoke test covered reads only) and the knee top point
    (validates est_dram on the knee axis).
- Knee caveat for Little's Law: at the top point N = 23.3 GB/s x 451 ns /
  64 B = ~164 lines in flight. That is plausible only with hardware
  prefetches counted (streaming threads keep many L2 prefetches
  outstanding). R is the chase probe's latency, not the mean over all
  requests: an approximation, to be stated in the report.
- Memory-access summaries average the whole process, so the steady phase
  was made long (~4-5 s), and the observed maximum is also used.
- Gate tests (stand-in VTune): 45/45 PASS when names or func@ addresses are
  present; FAIL correctly on a wrong function, a missing DRAM line or a failed
  collection. One stand-in limitation (it didn't know bw's function) was
  fixed in the stand-in, not the script. Stand-in output verified fake and
  removed.

### Correctness suite (`scripts/test.sh`, now 366 checks, all pass)
- lat single-cycle check: random/seq/page modes, 128 B .. 256 MB.
- **Mutation:** `lat_mutant` (Fisher-Yates instead of Sattolo, which can
  split into several cycles) fails the check at 64K/1M/16M as required.
- bw: 320 combinations (5 patterns x 4 mixes x 4 granularities x NT/regular
  x 2 sizes). Coverage (each line once), read checksum, exact written
  values, untouched bytes unchanged.
- kern vs double reference; asm checks; sanity ranges vs known values.
