# Working notes (findings to carry into REPORT.md)

## Batch 1 (fio runner, parser, validation)
- fio 3.43 (official Windows build), engine windowsaio, direct I/O. The
  safety self-test refuses raw devices, bare drives, paths outside
  C:\fio-test\, buffered writes, and runs below the 100 GB free floor.
- **Effective queue depth (pitfall, found in validation).** One fio thread
  at iodepth=32 delivered 54.5 K IOPS with Little's-Law QD 20.2. fio's own
  depth histogram: depth 8-16 for 93% of the time, 32 only 4%.
  - Submission costs ~17-21 us per I/O (slat), so one thread caps at
    ~1/17 us = 58 K IOPS.
  - 4 jobs x iodepth 8 gives effective QD 30.0 and 135.6 K IOPS.
  - Sweeps therefore scale depth with numjobs (<= 4) and report the
    effective QD.
- **QD=1 latency split:** 4 KiB random read 200 us total = 21 us Windows
  submission (slat) + 179 us completion.
- **Buffered-I/O pitfall:** psync buffered random 4 KiB reads on a warm
  256 MiB file: 186.5 K IOPS at 5.2 us, vs 4.5 K IOPS at 220 us direct
  (41x / 42x). The buffered sequential read showed 4.38 GB/s, above the
  PCIe 3.0 x4 ceiling: impossible for the SSD, so it came from RAM.
  - fio invalidates the cache before each job by default; the demo needs
    invalidate=0 + a warming pass.
  - windowsaio in buffered mode did NOT show the effect (overlapped I/O
    misses the cache fast path); only psync did.
  - Windows refuses direct I/O with synchronous engines.
- **Script bugs caught (fixed):**
  - a failed fio run left its error output in data/raw, which broke the
    parser. Failed outputs are now renamed *.json.failed and the parser
    names any bad file;
  - log paths (write_bw_log) must NOT have ':' escaped (only filename),
    otherwise the logs silently fail to open;
  - meta params now record fio defaults (ioengine/direct were blank);
  - the "knee" tail run duplicated mid QD when knee = 16 (now skipped and
    logged).
  - Test-harness mistakes (printf / heredoc backslashes mangled Windows
    paths) were caught by fio errors; job files are always written by
    Python.

### Major platform finding: writes are very slow; BitLocker is ON
- QUICK sweep (1 GiB file, QD 16):
  - reads: 128 KiB seq 686 MB/s, 4 KiB random 88 K IOPS;
  - writes: 128 KiB seq **55 MB/s**, 4 KiB random 34 K IOPS;
  - write p99 up to **202 ms**;
  - 1 MiB sequential writes 136-170 MB/s on new AND existing files (so
    NTFS file extension is NOT the cause, tested);
  - the bandwidth log shows 245 -> 73 MB/s within 4 s.
- **BitLocker / Device Encryption ON** (BitLockerProtection = 1).
  ~~"Software encryption caps the whole path"~~: **retracted.** That was
  stated without evidence (student concern 25). The tests below show reads
  are NOT CPU/encryption-bound.

### Investigation (student concerns 25-27), using existing data + read-only tests (no extra writes)
- **Writes: the 200 ms is not a lone outlier.**
  - 128 KiB seq writes at QD16: p50 35 ms, p99 202 ms.
  - 1 MiB writes at QD8: p50 31-50 ms, p99 139-209 ms.
  - 4 KiB random writes: p50 0.10 ms, p99 ~7 ms, p99.9 up to 279 ms.
  - **At QD1, writes are fast:** 4 KiB 50 us; one 128 KiB write 0.37 ms
    (p50); the max is 22-34 ms.
  - **Every write pattern converges on ~140-170 MB/s** (4K random QD16
    141 MB/s; 1 MiB seq 136-170 MB/s). Little's Law predicts the latency:
    16 x 128 KiB / 150 MB/s = ~14 ms vs measured p50 13-35 ms.
  - So the tails are queueing behind a **sustained write-throughput cap**.
    Fast QD1 writes argue against a disabled write cache or slow encryption.
    The drive buffers writes quickly but drains at ~150 MB/s (SLC-cache
    state or thermal: unverified).
- **Reads: not CPU-bound** (read-only scaling test, data/raw/investigate).
  - 1 MiB seq read, (jobs, depth) from (1,1) to (4,8): **flat 618-766
    MB/s**.
  - Total CPU only **8-14%** of 8 logical CPUs (~1 core); latency grows
    linearly with QD.
  - OS disk counters match fio within a few %.
  - Encryption/CPU would scale with threads and raise CPU. The ceiling is
    the **device or link**. ~750 MB/s usable fits a ~1 GB/s link (e.g. a
    down-trained PCIe link, x1 or lower generation, power saving).
  - **Unverified:** link width/speed is not exposed through the Intel VMD
    driver (DEVPKEY_PciDevice_* empty).
- **Pitfall made like-for-like** (same 256 MiB file, 4 KiB random read,
  QD1):

  | variant | IOPS | mean |
  |---|---|---|
  | direct windowsaio | 7,864 | 126 us |
  | buffered psync COLD | 10,680 | 93 us |
  | buffered psync WARM | 189,411 | 5.1 us |
  | buffered windowsaio WARM | 62,708 | 15.5 us |

  - The same engine, cold vs warm, gives a **24x** difference: the page
    cache, not the engine.
  - **Correction:** windowsaio buffered reads DO hit the cache once the file
    is cached. Earlier, the windowsaio warming pass did not populate the
    cache; psync's did.
  - Direct QD1 was 126 us here vs 220 us earlier on the same file: session
    variation, hence 3 reps.
- **Admin diagnostic** (data/raw/diag_admin.txt, read-only):
  - **device write cache ENABLED**: rules out a disabled cache as the cause
    of slow writes, consistent with fast QD1 writes;
  - BitLocker XTS-AES 128, software, "Used Space Only Encrypted", 100%;
  - SMART **temperature 60 C** right after the Batch 1 tests (warm; thermal
    throttling stays a plausible but unproven cause of the ~150 MB/s
    sustained write cap). TemperatureMax not reported. Wear 0%.
  - Temperature during sweeps would need admin; not logged (limitation).
### Full sweeps (2026-10-22 08:34-09:19), 0 failed runs
- 205 runs: prep 1, qd1 12, bs 96, mix 12, qd 54, tail 12, state 2,
  pitfall 15. Knees by the 90%-of-max-IOPS rule: randread QD32, randwrite
  QD4. **Total host writes 113.8 GB** (fio io_bytes; budget 163).
- QD1 (median of 3):
  - 4K randread 139 us (p50 134, p99 216, p99.9 399), slat 10.5 us;
  - 4K randwrite 69 us (p50 50, p99 136, p99.9 1810);
  - 128K seq read 179 us (730 MB/s);
  - 128K seq write mean 1316 us (p50 354, p99 30.5 ms).
- QD sweep, randread: linear to QD32 (126K IOPS, 242 us), plateau
  110-116K beyond with latency rising linearly. Effective QD (Little's Law)
  equals requested up to 32, then 51/95/176 at 64/128/256 (per-thread
  submission limit). Randwrite: 15-31K IOPS at all QDs (write cap),
  latency proportional to QD.
- Block size: reads saturate ~700-745 MB/s from 64 KiB; writes ~90-100 MB/s
  at every size from 16 KiB.
- Mix (4K, QD16): 84.4K / 46.8K / 24.8K / 21.3K IOPS for 100/70/50/0%
  reads; read p99 293 us -> 1.9 ms -> 4.1 ms as writes are added.
- Tail: randread p99.9/p50 = 9x (QD16), 16x (QD32 knee); randwrite QD16
  p99.9 30.5 ms vs p50 112 us (272x).
- State: the 16 GiB seq write peaked at 183 MB/s in second 1, median 133,
  min 42 MB/s over 133 s. **No SLC burst observable**: the ~130 MB/s cap
  holds from the first second (H7 burst-then-drop not seen). Steady 4K
  randwrite 126 MB/s.
- **Pitfall run 1 (kept as data/raw/pitfall_run1):**
  - rep 2 overlapped the student's status message and the assistant's
    check (09:17): all runs degraded;
  - rep 3: the warm pass read at 989 MB/s (from disk, not cache) and the
    buffered reads then missed the cache (2.5K IOPS, slower than direct).
- **Pitfall re-run (read-only, quiet):** buffered 177K / 54K / 169K IOPS vs
  direct 8.0K / 7.3K / 5.3K. The warm pass was cached every time (~5 GB/s).
  Buffered results swing 3x with OS cache state; direct stays steady. Median
  gap ~23x.
- run_sweeps.py printed nothing to the log until exit (Python output
  buffering when redirected): use `python -u` for live logs.

- **Wear policy** (student: don't increase write volume aggressively;
  system SSD).
  - SLC test 16 GiB (was 128); main test file 16 GiB (was 32: still far
    larger than any cache, and it halves the prep fill).
  - Write-side sweep runs 5 s + 1 s ramp, with 3 reps kept; tail write runs
    30 s (reads 60 s); steady state 120 s (was 300).
  - **Correction:** a first estimate of "60-80 GB" was wrong. Summing
    every write-side run at the measured ~150 MB/s gives an upper estimate
    of **~163 GB** (~0.05% of ~300 TBW). Actual totals will be reported from
    fio's io_bytes.
