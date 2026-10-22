# Experimental procedures

Fixed before Batch 2. Any deviation is recorded in `reports/NOTES.md`.

## Environment and control
- **Recording:** `scripts/capture_env.sh` writes `data/raw/env.txt`
  (CPU, caches, DIMMs/channels, page sizes, large-page privilege, VBS, tool
  versions). `data/raw/env_notes.txt` holds manual settings: power slider
  "Best performance", AC power.
- **Pinning:**
  - Single-thread tools pin to logical CPU 2 (physical core 1) with
    `SetThreadAffinityMask`. The process runs at `HIGH_PRIORITY_CLASS`, the
    thread at `THREAD_PRIORITY_HIGHEST`.
  - Multi-threaded tools use one logical CPU per physical core, in the order
    core 1, 2, 3, then 0 (core 0 last, since it services most OS interrupts).
    SMT siblings are used only when explicitly requested (`--smt`).
  - Topology is verified by `scripts/test.sh` (siblings are 2k, 2k+1).
- **Isolation:** Windows cannot isolate a core. Mitigations:
  - plugged in, no other heavy applications during runs;
  - interleaved ordering (below);
  - robust statistics.
- **Frequency:** turbo stays on, as on a normal laptop. The core clock is
  measured before and after every run (dependent-add loop) and stored with
  each row. Cycles = ns x measured GHz. The uncore/ring clock cannot be
  measured; Batch 1 showed it shifts DRAM latency after heavy load (NOTES.md).
- **Memory state:** every buffer is page-aligned and pre-touched before
  timing. Each run warms up (one lap of the chase, or one discarded
  bandwidth trial).

## Repetitions, ordering and statistics
- Every configuration runs as **R = 3 full repetitions** of its sweep. Each
  run contains several timed trials: `lat` 7, `bw` 5, `loaded` 7.
- **Ordering:** the repetition loop is outermost and the tool/variant loop
  innermost. Slow drift (thermal, background) then spreads across all
  variants instead of biasing one. Idle-latency points are never run directly
  after a heavy all-core run: sweeps that mix them insert a 2 s pause.
- **Per run:** median of trials, plus min and max.
- **Across repetitions:** median of run medians (central value). The error
  bars are min..max of the run medians.
- **Latency floors:** DRAM latency medians drift 10-25% between runs while
  minima are stable to ~1% (Batch 1). Latency tables therefore report
  **median and minimum**. Comparisons between separate runs (tests,
  control-minus-page differences) use the minimum.
- **Bandwidth:** median of trials. Error bars as above.
- **Per-process variation:** each repetition is a separate process, with
  different physical pages. DRAM latency varies between processes beyond
  within-run noise (Batch 2 gate: 112-137 ns idle). Results therefore report
  the spread across the 3 repetitions. Cross-tool or cross-run comparisons
  use the lowest minimum over several interleaved runs.
- **Interference flagging:** `typeperf` logs % busy per logical CPU every
  second during sweeps (`data/raw/cpu_log.csv`). A row is flagged when a CPU
  not used by the benchmark averages > 25% busy during its t_start..t_end;
  flagged rows are reported and re-run, not silently dropped.
- **In-cache bandwidth:** `bw`'s general pattern loop caps at ~83 GB/s, so
  its L1/L2 points are lower bounds. Cache bandwidth ceilings come from
  Project 1's `peak.c` on the same machine.

## Derived quantities (definitions fixed in advance)
- **Cycles:** ns x GHz, using the mean of the before/after measurement.
- **Useful bandwidth:** lines visited x bytes touched / time.
  **Estimated DRAM bandwidth:** lines x 64 B x (1 + write fraction), or x 1
  for non-temporal stores; validated against VTune measured DRAM bandwidth.
- **Knee:** lowest bandwidth at which median loaded latency exceeds 1.5x
  idle, interpolated. The 2x point is also reported.
- **Little's Law:** outstanding lines = bandwidth (B/s) x latency (s) / 64.
  Computed for every loaded point and every K-chain point. The K-chain
  plateau is compared with the core's fill-buffer count.
- **AMAT:** sum over levels of (fraction of loads served by that level x
  latency of that level), plus walks per load x walk penalty.
  - Fractions come from VTune `MEM_LOAD_RETIRED.{L1,L2,L3}_HIT/L3_MISS`.
  - Level latencies come from the zero-queue table.
  - Walk penalty is the page-mode minus control difference.
- **TLB cost:** page-mode latency minus control latency, at equal node count.
- **Counter correlation:** per-element counts (counter / elements
  processed) vs ns per element. Reported as Spearman rank correlation
  overall and per regime, with Pearson r alongside.

## Counters (VTune, administrator)
- Run only through `scripts/vtune_smoke.sh` and `scripts/run_counters.sh`
  from an elevated Git Bash.
- **Validated before use:**
  - instructions vs the loop's instruction count x iterations;
  - `L2_LINES_IN.ALL` vs expected lines (bytes / 64);
  - measured DRAM bandwidth vs `bw`.
- **Multiplexing:** with more programmable events than hardware counters,
  VTune time-multiplexes and scales the counts. Runs last >= 3 s so the
  scaled counts are stable; this is noted as a caveat.

## Reproducibility
- Every number in the report comes from a CSV in `data/raw/` produced by a
  script in `scripts/`, via `scripts/analyze.py`.
- `scripts/test.sh` must pass before sweeps run. The commit hash is logged in
  `data/raw/correctness.log`.
