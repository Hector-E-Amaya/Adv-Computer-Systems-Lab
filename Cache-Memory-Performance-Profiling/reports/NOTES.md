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

### Correctness suite (`scripts/test.sh`, now 361 checks, all pass)
- lat single-cycle check: random/seq/page modes, 128 B .. 256 MB.
- **Mutation:** `lat_mutant` (Fisher-Yates instead of Sattolo, which can
  split into several cycles) fails the check at 64K/1M/16M as required.
- bw: 320 combinations (5 patterns x 4 mixes x 4 granularities x NT/regular
  x 2 sizes). Coverage (each line once), read checksum, exact written
  values, untouched bytes unchanged.
- kern vs double reference; asm checks; sanity ranges vs known values.
