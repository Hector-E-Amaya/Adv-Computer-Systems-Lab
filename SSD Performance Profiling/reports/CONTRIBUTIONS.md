# Student review contributions (factual log), Project 3

Format as in Projects 1-2: concern, verdict after checking, outcome. Items
found by the assistant's own testing are in NOTES.md.

| # | Date | Concern (student) | Verdict after checking | Outcome |
|---|---|---|---|---|
| 25 | 2026-10-22 | "BitLocker may not be the primary bottleneck": slow reads/writes do not establish BitLocker as the cause | **Valid; the assistant's claim was unsupported** | Read-only scaling test: read throughput flat at ~620-770 MB/s from 1 to 4 threads at 8-14% total CPU, so NOT CPU/encryption-bound. The claim was retracted in NOTES; the ceiling is device/link-side (link not verifiable through VMD) |
| 26 | 2026-10-22 | Check whether 200 ms writes are outliers or tail percentiles, QD/time patterns, background activity, file initialisation; do not raise write volume (system SSD wear) | Valid | From existing data only: the 200 ms is p99 (not a lone max) for large writes. QD1 writes are fast. All write patterns converge on a ~150 MB/s sustained cap, and Little's Law predicts the tails (queueing). Write volume was reduced (16 GiB SLC test, 8 s write runs, 120 s steady state) |
| 27 | 2026-10-22 | The buffered-I/O pitfall must compare like-for-like workloads | Valid: the engines differed (psync vs windowsaio) | Same file/bs/QD with the same engine, cold vs warm cache: 10.7 K vs 189 K IOPS (24x), so the cache is the cause, not the engine. This also corrected the earlier note that windowsaio buffered reads miss the cache |
