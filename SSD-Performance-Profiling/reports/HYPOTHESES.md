# Pre-experiment Hypotheses

Committed **before fio was installed or any I/O was measured**; the git
history is the evidence. Estimates come from the drive class (Samsung PM991a,
a DRAM-less PCIe 3.0 x4 NVMe client SSD with an SLC write cache, using host
memory as a mapping-table buffer), the interface, and the platform (Windows
11, NTFS file, Intel RST/VMD driver, fio with direct I/O).

## Setup
| Item | Value |
|---|---|
| SSD | NVMe SAMSUNG MZVLQ512HBLU-00BH1 (PM991a, 512 GB), firmware HPS1NFAV |
| Interface | PCIe 3.0 x4 (~3.9 GB/s raw, ~3.5 GB/s usable) behind Intel RST/VMD |
| Target | 32 GiB disposable file in C:\fio-test\ (NTFS, 4 KiB clusters), direct I/O |
| Host | i3-1125G4, 4C/8T, 8 GB single-channel DDR4, Windows 11 |

## H1. QD=1 latency (zero-queue)
| workload | predicted mean | p99 |
|---|---|---|
| 4 KiB random read | **90-130 us** (NAND read ~60-80 us + mapping lookup + Windows/RST stack) | ~2x mean |
| 4 KiB random write | **20-40 us** (acknowledged from the SLC/write buffer) | 3-5x mean (occasional flush) |
| 128 KiB sequential read | 100-200 us | ~1.5x |
| 128 KiB sequential write | 80-150 us | ~2x |

Writes faster than reads at QD=1 is the expected counter-intuitive result:
writes complete when buffered, reads must reach NAND.

## H2. Peak IOPS and throughput
- 4 KiB random read: 200-350 K IOPS at high QD (spec ~350 K); Windows + VMD
  overhead may cap below spec on 4 cores.
- 4 KiB random write (in SLC): 250-400 K IOPS.
- Sequential read (128 KiB+): **~3.0-3.1 GB/s**, close to the PCIe 3.0 x4
  ceiling.
- Sequential write: ~1.8 GB/s while the SLC cache lasts.

## H3. Block size
IOPS falls and MB/s rises with block size, saturating at 128-256 KiB near
the interface/NAND limit. Latency grows roughly in proportion to size once
bandwidth-bound.

## H4. Read/write mix (fixed 4 KiB random, mid QD)
Mixed workloads deliver less total IOPS than pure read or pure write.
70/30 > 50/50. Reads in a mix see higher tail latency, because they queue
behind writes and garbage collection.

## H5. Queue depth / parallelism, knee, Little's Law
- 4 KiB random read IOPS scale ~linearly up to QD 8-16, then saturate by
  QD 32-64. The **knee is ~QD 16-32**.
- Latency is flat before the knee and then grows ~linearly with QD.
- Little's Law holds throughout: **QD ~= IOPS x mean latency** (within ~10%).

## H6. Tail latency
At mid QD, p99/p50 is 2-3x. Near the knee, p99.9 is **5-10x p50**. Writes
show the largest tails (cache flushes, GC).

## H7. Device state
- A long sequential write runs at SLC speed (~1.8 GB/s) for **tens of GB**
  (dynamic SLC, sized by free space), then **drops to TLC speed, ~0.4-0.8
  GB/s**.
- Random writes after sustained writing (steady state) show lower IOPS and
  heavier tails than fresh/burst.
- A thin laptop may thermally throttle in long sequential runs.

## H8. Misleading setup (pitfall)
Buffered I/O (`direct=0`) on a working set that fits in RAM reports
impossibly high read IOPS (> 1 M) and single-digit-us latencies. That is the
page cache, not the SSD. Similarly, `iodepth` with a synchronous engine is
not a real queue depth: effective QD = 1.

## What would falsify these
- QD=1 4 KiB read under 50 us or over 300 us.
- Sequential read above the ~3.5 GB/s interface ceiling (a measurement
  error).
- No saturation knee up to QD 256.
- No write-speed drop after many tens of GB.
- Little's Law off by more than ~20% (a sign of a wrong effective QD or
  mis-measured latency).
