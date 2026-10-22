# Hypothesis review (pre-sweep addendum)

`HYPOTHESES.md` (commit d7ebccb) is left unchanged as the pre-registration.
This addendum records corrections found on review **before any Batch 2
sweep**. Disclosure: Batch 1 sanity checks had already produced a few
single-point numbers (L1 5.1 cycles, L2 14.1, L3 64.7, DRAM ~106-115 ns;
`reports/NOTES.md`). No sweep data existed. The original predictions are
still the ones scored in the final report. The notes below state what each
prediction does and does not test.

## Machine
- **Single channel confirmed** two ways.
  - Inventory: one 8 GB SODIMM in a 2-slot board. Total physical memory is
    8,241,053,696 bytes (7.67 GiB usable; the rest is reserved for iGPU and
    firmware), so there is no soldered memory.
  - Measurement: 4-thread read is 22.6 GB/s, below the 25.6 GB/s
    single-channel peak (dual channel would allow 51.2).

## H1: qualifications
- Above ~256 KB (L1 DTLB reach with 4 KB pages), every "cache-level" latency
  also contains TLB-miss cost. Above ~8 MB (STLB reach) it contains page
  walks. The reported per-level latencies are therefore "latency with 4 KB
  pages", and DRAM latency includes page-walk time.
- L3 is shared and non-inclusive. Its usable capacity for one core is below
  8 MB, so the L3-to-DRAM transition is expected to be gradual, from ~4 MB.

## H2: counting must be explicit
- `bw` reports two quantities:
  - **useful bytes:** lines x bytes touched;
  - **estimated DRAM bytes:** one line read per visit, plus one write-back per
    written line for regular stores.
- The estimate is a model. It assumes a read-for-ownership on every regular
  store and no wasted prefetches. It will be **validated with VTune's measured
  DRAM traffic** for representative configurations.
- "64 B stride matches sequential" is true by construction: stride 64 *is*
  the sequential order in `bw`. It is not reported as a finding.
- **Granularity:** "8 B vs 64 B per line costs the same DRAM time" is a
  prediction about line-granular DRAM transfer. It is tested with useful and
  estimated-DRAM bytes side by side.

## H3: objective knee definition (fixed before measuring)
- **Knee:** the lowest measured bandwidth at which median loaded latency
  exceeds **1.5x the idle latency** of the same session. Linear interpolation
  between neighbouring points is used.
- We also report the bandwidth at 2x idle, and the fraction of peak
  bandwidth at each point. The 60-75% prediction is scored against the 1.5x
  knee.
- **Little's Law** is applied at every point: outstanding lines =
  bandwidth x latency / 64 B.

## H4: counter selection and correlation claim
- `MEM_LOAD_RETIRED.L3_MISS` counts only *demand* loads that retire after
  missing L3. For streaming kernels, hardware prefetch converts most of those
  into hits, so demand misses can be far below the true DRAM line count.
- The counter set is therefore extended with `L2_LINES_IN.ALL` (all lines
  filled into L2, prefetches included). It is validated against the expected
  lines per pass, bytes / 64 B. VTune's memory-access analysis gives measured
  DRAM bandwidth.
- A Pearson r > 0.9 across footprints would be inflated by the few large
  cache-level steps. We report **Spearman rank correlation** and per-regime
  results alongside it.

## H5: AMAT qualification
- The original model used capacity-based hit fractions. With counters, the
  hit fractions come from measured `MEM_LOAD_RETIRED.{L1,L2,L3}_HIT/L3_MISS`.
  A TLB term is added: page walks per load x the measured page-walk penalty
  (from H6).
- The 15% target is kept as pre-registered and scored as is. Known reasons it
  may fail: overlapping misses, non-inclusive L3, replacement effects.

## H6: corrections
1. **Cache-set conflicts** are already avoided. Each page's node is placed at
   line offset (7 x page index) mod 64. 7 is coprime with 64, so nodes cycle
   through all 64 L1 sets. L2 (physically indexed) and L3 (hashed) spread them
   further.
   **CORRECTION (VTune smoke test, still before any sweep): the statement
   above was wrong.**
   - The L2 set index uses address bits 6-15: the slot plus the low 4
     page-frame bits. With consecutive frames, both depended only on
     (page index mod 64), so nodes used **64 of 1024 L2 sets**.
   - VTune showed 25% of page-mode loads going to DRAM for a 512 KB data
     footprint. Latency at 8192 pages was 26.7-44.5 ns, against 14.6 ns after
     the fix.
   - Fixed with slot = (i + 7*(i/64)) mod 64: exact L1 balance per 64 pages,
     all 1024 L2 (slot, frame) pairs per 1024 pages. Checked by
     `lat --check`, with the old layout kept as a failing mutant.
2. **Data-cache confound (new control).** In page mode the data footprint is
   one line per page. It outgrows L1 (48 KB = 768 lines) at about **768
   pages**, between the two predicted TLB steps (64 and ~2048). A step there
   could be data-cache, not TLB.
   - **Control:** the same number of nodes as a random chase over a compact
     region (`lat --mode random --size N*64`). It has the same data-cache
     behaviour and needs N/64 pages instead of N.
   - **TLB cost = page-mode latency - control latency** at equal N.
   - VTune `DTLB_LOAD_MISSES.WALK_COMPLETED` per load confirms which steps are
     TLB.
3. **Large pages: the original numbers were wrong.** On Willow Cove the L1
   DTLB has 32 entries for 2 MB pages, not 64. Its reach is 32 x 2 MB =
   **64 MB**, not "512x further". The STLB is shared between 4 KB and 2 MB
   entries (~2048), so its reach for 2 MB pages is several GB. That is beyond
   any buffer used here, so no STLB step is expected with large pages. Large
   pages remain conditional on the student granting SeLockMemoryPrivilege.
4. The first step may come slightly before 64 pages, because code, stack and
   the timing variables also occupy DTLB entries.

## H7
Predicted magnitudes (10x under-report etc.) are estimates, not guarantees.
They are scored as stated.
