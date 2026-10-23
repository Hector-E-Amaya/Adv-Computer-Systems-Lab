# Re-run check (2026-10-22)

A fresh clone of commit 5aa6d73 was built and re-measured on 2026-10-22
(same laptop, AC power, idle). The committed data were not touched.
`RESULTS_TABLES_rerun.md` is `scripts/analyze.py` run on a 1-repetition
re-sweep (`scripts/run_sweeps.sh` with REPS=1). `targeted_remeasure.csv`
holds 5-repetition re-measurements of the points that moved, and
`peaks_rerun.csv` the re-measured ceilings.

Verdict and discussion: `reports/REPORT.md` (re-run notes).

## Re-run vs committed

| table | values | median new/old | within 10% | largest deviation (row, column: old -> new) |
|---|---|---|---|---|
| Hypothesis checks (automatic; the verdict is only a range test) | 16 | 1.000 | 94% | H5 / worst misaligned/aligned time, avx512 f32, L2 / 1..1.3 / NOT consistent, value: 1.54 -> 1.9 |
| Speedup vs scalar by cache regime (median over sizes in the regime) | 160 | 1.000 | 98% | stencil / f64 / L1, avx512: 3.97 -> 4.55 |
| GFLOP/s by cache regime | 160 | 0.992 | 96% | stencil / f32 / L3, avx512: 28.16 -> 23.42 |

## Targeted follow-up

| project | point | committed | re-run (1 rep) | targeted re-measure | verdict |
|---|---|---|---|---|---|
| 1 | avx512 L2 misalignment, saxpy f32, worst | 1.54x | 1.90x | 1.48-1.49x median (1.03-1.51 per rep) | re-run was an outlier; cost is bimodal per rep |
