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
- **BitLocker / Device Encryption ON** (BitLockerProtection = 1). Software
  encryption on the CPU caps the whole path (reads ~0.7 GB/s vs the drive's
  ~3 GB/s).
- 200 ms write tails suggest more than encryption cost: device write cache
  policy and/or thermal state. The read-only admin diagnostic
  (scripts/diag_admin.sh) collects the cache policy, BitLocker method and
  SMART temperature/wear before the sweeps run.
