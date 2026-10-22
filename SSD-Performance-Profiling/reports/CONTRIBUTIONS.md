# Student review contributions (factual log), Project 3

Format as in Projects 1-2: concern, verdict after checking, outcome. Items
found by the assistant's own testing are in NOTES.md.

| # | Date | Concern (student) | Verdict after checking | Outcome |
|---|---|---|---|---|
| 25 | 2026-10-22 | "BitLocker may not be the primary bottleneck": slow reads/writes do not establish BitLocker as the cause | **Valid; the assistant's claim was unsupported** | Read-only scaling test: read throughput flat at ~620-770 MB/s from 1 to 4 threads at 8-14% total CPU, so NOT CPU/encryption-bound. The claim was retracted in NOTES; the ceiling is device/link-side (link not verifiable through VMD) |
| 26 | 2026-10-22 | Check whether 200 ms writes are outliers or tail percentiles, QD/time patterns, background activity, file initialisation; do not raise write volume (system SSD wear) | Valid | From existing data only: the 200 ms is p99 (not a lone max) for large writes. QD1 writes are fast. All write patterns converge on a ~150 MB/s sustained cap, and Little's Law predicts the tails (queueing). Write volume was reduced (16 GiB SLC test, 8 s write runs, 120 s steady state) |
| 28 | 2026-10-22 | Faster QD1 writes don't prove an SLC cache is responsible | Valid | Report says "write buffer / device write cache (enabled)", not "SLC" |
| 29 | 2026-10-22 | Constant ~0.7 GB/s read and ~0.1 GB/s write ceilings suggest a bottleneck beyond NAND/interface limits | Valid | Write runs are also NOT CPU-bound (total CPU median 10%, p90 24%; reads median 18%). Both ceilings sit on the device/link side; candidate causes (link/power state behind VMD, driver path, thermal/internal state) are reported as hypotheses, not verified |
| 30 | 2026-10-22 | 84K mix read IOPS vs 126K peak: same conditions? | Checked: different QD | Mix runs are QD16; the QD sweep at QD16 gives 82.5-83.0K (matches). 126K is at QD32 |
| 31 | 2026-10-22 | Is the 272x write tail repeated across trials? | Valid check: yes | p99.9/p50 per rep 195x / 272x / 272x at QD16 (p99.9 21.6-30.8 ms); write knee QD4 varies 115-378x; read ratios stable (8-9x, 16-18x) |
| 32 | 2026-10-22 | No initial write burst doesn't prove the SLC cache isn't working | Valid | Reported as "not observable through an upstream ~140 MB/s cap", not as absence of an SLC cache |
| 27 | 2026-10-22 | The buffered-I/O pitfall must compare like-for-like workloads | Valid: the engines differed (psync vs windowsaio) | Same file/bs/QD with the same engine, cold vs warm cache: 10.7 K vs 189 K IOPS (24x), so the cache is the cause, not the engine. This also corrected the earlier note that windowsaio buffered reads miss the cache |
