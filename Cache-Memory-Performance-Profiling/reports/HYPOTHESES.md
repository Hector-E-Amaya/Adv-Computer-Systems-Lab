# Pre-experiment Hypotheses

Written and committed **before any benchmark was written or run**; the git
history is the evidence of ordering. Numbers are estimates, from three
sources:
- published Tiger Lake (Willow Cove) characteristics;
- this machine's configuration (`data/raw/env.txt`);
- the bandwidth already measured on this same laptop in Project 1
  (`../SIMD-Advantage-Profiling/data/raw/peaks.csv`).

## Machine
| Item | Value |
|---|---|
| CPU | Intel Core i3-1125G4 (Tiger Lake), 4 cores / 8 threads, ~3.7 GHz turbo (3.68 GHz median measured in Project 1) |
| L1D / L2 / L3 | 48 KB per core / 1.25 MB per core / 8 MB shared |
| TLB (published, approximate) | L1 DTLB 64 entries (4 KB pages); shared STLB ~2048 entries; fewer entries for 2 MB pages |
| Memory | one 8 GB DDR4-3200 SODIMM in a 2-slot system: **single channel**, 25.6 GB/s theoretical |
| Pages | 4 KB base; 2 MB large pages supported, privilege not yet granted |
| OS / tools | Windows 11 Home, gcc 16.2.0 (MSYS2 UCRT64), Intel VTune for counters |

Cycle conversions below assume about 3.6 GHz (1 ns ≈ 3.6 cycles).

## H1. Zero-queue latency (random pointer chase, one thread)
| Level | Predicted cycles | Predicted ns | Reasoning |
|---|---|---|---|
| L1 | 5 | ~1.4 | published load-to-use latency for Willow Cove |
| L2 | 13-16 | ~4 | published ~14 cycles |
| L3 | 40-70 | 11-20 | ring + LLC slice; client parts sit at the lower end |
| DRAM | 300-400 | **85-110** | DDR4-3200 laptop; adds L3 miss path + controller + DRAM row access |

The transitions should appear at the cache capacities: 48 KB, 1.25 MB, and
somewhat below 8 MB, since L3 is shared and inclusive of other data. From
roughly 256 KB up, the curve also carries TLB-miss cost: the L1 DTLB reach is
64 x 4 KB = 256 KB, the STLB reach about 8 MB. So "DRAM latency" measured with
4 KB pages will include page-walk time. This is the reason for H6.

## H2. Bandwidth by pattern, granularity and read/write mix
- **Sequential read, one thread:** ~20 GB/s. Project 1 measured 20.8 GB/s
  with 512-bit loads.
- **Sequential read, all 4 cores:** 22-24 GB/s, 85-95% of the 25.6 GB/s
  single-channel peak. One core already nearly saturates a single channel,
  so extra threads add little.
- **100% write (regular stores):** about half the read figure in *useful*
  bytes, because every written line is first read (write-allocate). Project
  1 measured 8.8 GB/s for writes.
- **70/30 and 50/50 mixes:** total traffic below pure read. DRAM
  read/write bus turnarounds cost time. The 50/50 mix should be worst among
  the mixes.
- **Stride, at equal bytes touched per line:**
  - 64 B stride touches every line and should match sequential in
    lines/second;
  - 256 B stride stays prefetch-friendly within a page;
  - 1024 B stride touches 4 lines per 4 KB page, so prefetch helps less and
    line throughput drops.
  - Useful bandwidth falls with stride, because each 64 B line yields only
    the bytes actually used.
- **Random line order:** lowest line throughput per thread. Each core can
  keep only ~12 demand misses in flight (fill buffers). By Little's Law,
  12 x 64 B / ~100 ns ≈ 7-8 GB/s per thread.
- **Granularity:** within one line, touching 8 B vs 64 B costs about the
  same DRAM time (a whole line moves either way). Useful GB/s therefore
  scales with the bytes used per line.

## H3. Throughput-latency knee (loaded latency)
- Latency stays near idle (H1) until about **60-75% of peak bandwidth**, then
  rises steeply. Near saturation it should reach **2-3x idle**, while
  bandwidth gains flatten (diminishing returns).
- **Little's Law check:** at ~20 GB/s and ~100 ns, about 20e9 x 100e-9 / 64
  ≈ **31 lines** are in flight system-wide.
- **Single-thread version (K independent pointer chains):** throughput grows
  about linearly with K up to **K ≈ 10-12** (the fill-buffer limit), then
  flattens. Latency rises once K exceeds that limit.

## H4. Working set and cache misses (with VTune counters)
- For saxpy/mul, time per element steps up at each cache boundary. In the
  DRAM regime, time per element should track LLC misses per element
  linearly, with a strong correlation (r > 0.9).
- **Expected miss counts, sequential fp32 saxpy:** about 2 arrays x 4 B /
  64 B = 1/8 line per element per pass. Hardware prefetching will turn many
  demand misses into prefetch hits. So counted *demand* LLC misses may be
  well below the line count, while DRAM traffic still equals it. This is a
  caveat to examine, not hide.

## H5. AMAT
A random chase whose footprint lies between two cache sizes should have an
average latency predicted by AMAT from the per-level latencies (H1) and the
fraction of lines resident in each level, to within ~15%. Where the
prediction fails, the extra time is attributed to TLB misses (H6).

## H6. TLB
- **Page-mode chase** (one line per 4 KB page, so the data itself fits in
  cache):
  - flat latency up to **64 pages**;
  - a small step after 64 pages (STLB hit, ~+7 cycles);
  - a large step after **~2048 pages** (page walk, +20-40 cycles, more if
    page-table entries miss in cache).
- **With 2 MB large pages** (if the privilege is granted), both steps move
  out by 512x in footprint. The DRAM-regime random-chase latency should drop
  by the page-walk share, roughly 10-25%.

## H7. Benchmark pitfalls (predicted magnitude)
- **Sequential instead of random pointer chase:** the prefetcher hides DRAM
  latency, so a "DRAM latency" of only a few ns is reported. That
  under-reports by **10x or more**.
- **Too-small buffer** (reuse instead of DRAM): reports L2/L3 latency as
  "memory latency".
- **Unpinned threads:** higher variance (larger CV across repetitions) and
  occasional outliers from migration. Medians move only a little.

## Expected noise
- Laptop turbo and thermals (Project 1 saw 2.81-3.69 GHz).
- Windows background activity. DRAM-regime results noisier than cache.
- Prefetchers cannot be disabled on Windows without privileged MSR access;
  random patterns are used where the true latency is needed.
- VBS/memory integrity is on, and may restrict VTune hardware sampling.

## What would falsify these
- L1 latency far from 5 cycles: suggests a measurement or frequency error;
  check the method before believing it.
- Single-thread sequential read far below 15 GB/s, or any result above
  25.6 GB/s (impossible on one channel).
- No knee: latency flat until saturation, or rising from zero load.
- K-chain throughput still rising past K = 16.
- No TLB step near 64 or ~2048 pages.

Any of these would be reported and explained, not hidden.
