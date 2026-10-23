# Re-run check (2026-10-22)

Re-run on 2026-10-22 in the same boot as the sweeps (AC power, idle).
`qd1.csv`, `qd.csv` and `bs.csv` are parsed results of a 3-repetition subset
run through the project's own runner from a fresh clone (`run.log`; 4.28 GB
written). `../recheck_ceiling/` holds a later read-only follow-up of five
back-to-back repetitions (`ceiling_run.log`). `powercfg_pcie_aspm.txt` is
the read-only Windows PCIe power setting.

Verdict and discussion: `reports/REPORT.md` (re-run notes).

## Re-run vs committed

| run | metric | committed (median of 3) | re-run (median of 3) | new/old |
|---|---|---|---|---|
| qd1/randread_4k_qd1 | iops | 7169.9 | 7167.6 | 1.000 |
| qd1/randwrite_4k_qd1 | iops | 14339.5 | 14800.2 | 1.032 |
| qd1/read_128k_qd1 | iops | 5566.1 | 4116.4 | 0.740 |
| qd1/write_128k_qd1 | iops | 752.7 | 820.4 | 1.090 |
| qd1/randread_4k_qd1 | lat_mean_us | 138.6 | 138.6 | 1.000 |
| qd1/randwrite_4k_qd1 | lat_mean_us | 68.5 | 66.4 | 0.969 |
| qd1/read_128k_qd1 | lat_mean_us | 178.7 | 241.9 | 1.354 |
| qd1/write_128k_qd1 | lat_mean_us | 1315.7 | 1207.4 | 0.918 |
| qd1/randread_4k_qd1 | read_p99_us | 216.1 | 228.3 | 1.057 |
| qd1/read_128k_qd1 | read_p99_us | 423.9 | 501.8 | 1.184 |
| qd1/randwrite_4k_qd1 | write_p99_us | 136.2 | 111.1 | 0.816 |
| qd1/write_128k_qd1 | write_p99_us | 30539.8 | 30277.6 | 0.991 |
| qd/randread_4k_qd1 | iops | 6936.5 | 6779.4 | 0.977 |
| qd/randread_4k_qd128 | iops | 113209.5 | 115006.5 | 1.016 |
| qd/randread_4k_qd16 | iops | 82714.7 | 83163.1 | 1.005 |
| qd/randread_4k_qd2 | iops | 13789.2 | 13596.4 | 0.986 |
| qd/randread_4k_qd256 | iops | 110070.0 | 113350.0 | 1.030 |
| qd/randread_4k_qd32 | iops | 125984.5 | 128840.5 | 1.023 |
| qd/randread_4k_qd4 | iops | 26306.7 | 26401.7 | 1.004 |
| qd/randread_4k_qd64 | iops | 115831.1 | 117380.7 | 1.013 |
| qd/randread_4k_qd8 | iops | 48012.9 | 48097.1 | 1.002 |
| qd/randwrite_4k_qd16 | iops | 25914.0 | 25140.8 | 0.970 |
| qd/randread_4k_qd1 | lat_mean_us | 143.3 | 146.7 | 1.024 |
| qd/randread_4k_qd128 | lat_mean_us | 834.9 | 829.5 | 0.994 |
| qd/randread_4k_qd16 | lat_mean_us | 188.7 | 187.6 | 0.994 |
| qd/randread_4k_qd2 | lat_mean_us | 143.3 | 145.3 | 1.014 |
| qd/randread_4k_qd256 | lat_mean_us | 1570.9 | 1544.5 | 0.983 |
| qd/randread_4k_qd32 | lat_mean_us | 242.4 | 236.9 | 0.977 |
| qd/randread_4k_qd4 | lat_mean_us | 148.4 | 148.1 | 0.998 |
| qd/randread_4k_qd64 | lat_mean_us | 432.8 | 432.4 | 0.999 |
| qd/randread_4k_qd8 | lat_mean_us | 162.7 | 162.2 | 0.997 |
| qd/randwrite_4k_qd16 | lat_mean_us | 597.8 | 616.5 | 1.031 |
| qd/randread_4k_qd1 | read_p99_us | 226.3 | 216.1 | 0.955 |
| qd/randread_4k_qd128 | read_p99_us | 8224.8 | 8093.7 | 0.984 |
| qd/randread_4k_qd16 | read_p99_us | 297.0 | 301.1 | 1.014 |
| qd/randread_4k_qd2 | read_p99_us | 207.9 | 203.8 | 0.980 |
| qd/randread_4k_qd256 | read_p99_us | 18481.2 | 20054.0 | 1.085 |
| qd/randread_4k_qd32 | read_p99_us | 456.7 | 456.7 | 1.000 |
| qd/randread_4k_qd4 | read_p99_us | 209.9 | 207.9 | 0.990 |
| qd/randread_4k_qd64 | read_p99_us | 3063.8 | 2867.2 | 0.936 |
| qd/randread_4k_qd8 | read_p99_us | 238.6 | 236.5 | 0.991 |
| qd/randwrite_4k_qd16 | write_p99_us | 16908.3 | 13041.7 | 0.771 |
| bs/randread_128k_qd16 | iops | 5399.0 | 3739.8 | 0.693 |
| bs/randread_1m_qd16 | iops | 698.6 | 432.3 | 0.619 |
| bs/randread_4k_qd16 | iops | 83791.4 | 83722.9 | 0.999 |
| bs/read_128k_qd16 | iops | 5557.4 | 4103.6 | 0.738 |
| bs/read_1m_qd16 | iops | 707.7 | 488.3 | 0.690 |
| bs/read_4k_qd16 | iops | 79840.9 | 80360.1 | 1.007 |
| bs/randread_128k_qd16 | lat_mean_us | 2961.3 | 4275.2 | 1.444 |
| bs/randread_1m_qd16 | lat_mean_us | 22855.1 | 36892.4 | 1.614 |
| bs/randread_4k_qd16 | lat_mean_us | 186.3 | 186.4 | 1.001 |
| bs/read_128k_qd16 | lat_mean_us | 2875.7 | 3898.0 | 1.355 |
| bs/read_1m_qd16 | lat_mean_us | 22482.7 | 32735.0 | 1.456 |
| bs/read_4k_qd16 | lat_mean_us | 194.0 | 192.4 | 0.992 |
| bs/randread_128k_qd16 | read_p99_us | 16318.5 | 25559.0 | 1.566 |
| bs/randread_1m_qd16 | read_p99_us | 121110.5 | 204472.3 | 1.688 |
| bs/randread_4k_qd16 | read_p99_us | 301.1 | 301.1 | 1.000 |
| bs/read_128k_qd16 | read_p99_us | 15925.2 | 25821.2 | 1.621 |
| bs/read_1m_qd16 | read_p99_us | 126353.4 | 189792.3 | 1.502 |
| bs/read_4k_qd16 | read_p99_us | 370.7 | 362.5 | 0.978 |

## Targeted follow-up

| project | point | committed | re-run (1 rep) | targeted re-measure | verdict |
|---|---|---|---|---|---|
| 3 | 1 MiB seq read QD16, MB/s | 744 | 515 | 793, 782, 598, 563, 572 (back-to-back reps) | falls ~28% within ~1.5 min of sustained reads |
| 3 | 4 KiB random read QD32, K IOPS | 126.0 | 128.8 | 147.5, 131.0, 125.8, 122.9, 123.8 | same load-dependent drop |
