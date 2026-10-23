# Re-run check (2026-10-22)

A fresh clone of commit 5aa6d73 was built and re-measured on 2026-10-22
(same laptop, AC power, idle). The committed data were not touched.
`RESULTS_TABLES_rerun.md` is `scripts/analyze.py` run on a 1-repetition
re-sweep (`scripts/run_sweeps.sh` with REPS=1; counter fractions were not
re-measured). `targeted_remeasure.csv` holds 9-process re-measurements of
the points that moved.

Verdict and discussion: `reports/REPORT.md` (re-run notes).

## Re-run vs committed

| table | values | median new/old | within 10% | largest deviation (row, column: old -> new) |
|---|---|---|---|---|
| 1. Zero-queue latency (random pointer chase, 4 KB pages) | 3 | 0.998 | 100% | L2 / 128 KB / 3.83-3.83, ns (median trial): 3.85 -> 3.83 |
| 2. TLB: page mode (one line per 4 KB page) vs compact control | 34 | 1.000 | 85% | 0/99/0/0 / 1/99/0/0, TLB cycles: 7.05 -> 9.59 |
| 3. Memory-level parallelism (K independent chases, 256 MB) | 4 | 1.000 | 100% | , GB/s: 5.86 -> 5.91 |
| 4. Loaded latency and the knee | 19 | 1.001 | 95% | 0%, probe latency ns: 282.4 -> 319.52 |
| 5. Bandwidth by pattern and read/write mix (256 MB, 64 B per line) | 64 | 0.990 | 100% | stride 1024 B / 0%, useful GB/s: 10.44 -> 9.63 |
| 6. Cache-miss impact: kernels across footprints (VTune counters) | 89 | 1.000 | 100% | saxpy / 2K, footprint B: 16384 -> 16384 |
| 7. AMAT reconstruction | 67 | 1.000 | 88% | 8M / , error %: 7.32 -> 31.33 |
| 8. Counter validation and evidence runs | 12 | 1.000 | 100% | saxpy instr/elem (loop = 6 instr / 8 floats), measured: 0.7524 -> 0.7524 |
| 9. Benchmark pitfalls | 15 | 1.000 | 73% | unpinned, CV %: 0.65 -> 5.08 |

## Targeted follow-up

| project | point | committed | re-run (1 rep) | targeted re-measure | verdict |
|---|---|---|---|---|---|
| 2 | TLB cost, 1024 pages | 1.92 ns | 2.61 ns | 1.91-1.94 ns (9 processes) | re-run was an outlier |
| 2 | latency at 8 MB (= L3 size) | 40.7 ns | 32.6 ns | 35.1-42.0 ns, median 37.9 (9 processes) | placement-sensitive; AMAT error +4% to +25% |
