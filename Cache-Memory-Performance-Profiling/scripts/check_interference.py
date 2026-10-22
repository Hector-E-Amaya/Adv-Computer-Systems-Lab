"""Flag sweep rows measured while other CPU activity was high.

For every row of data/raw/sweep_*.csv (each has t_start/t_end in epoch
seconds), average each logical CPU's % busy from data/raw/cpu_log.csv
(typeperf, 1 s samples) over the row's window, then take the busiest CPU the
benchmark did NOT use. The row is flagged when that exceeds THRESHOLD
(METHODS.md: 25%).

Benchmark CPUs come from the row itself:
  lat / kern: `cpu` (-1 = unpinned: the busiest CPU is assumed to be the
              benchmark, and only the rest are considered)
  bw:         `cpus` (space-separated)
  loaded:     `chase_cpu` + `traffic_cpus`
The SMT sibling of a benchmark CPU is NOT excluded: activity there shares
the benchmark's core, which is exactly the interference to catch.

A typeperf sample stamped t covers roughly (t-1, t], so the window uses
samples with t_start < t <= t_end + 1. The first typeperf row is blank and is
skipped.

Output: data/processed/interference.csv (one line per row) and a summary.
Usage: python scripts/check_interference.py [--suffix _quick] [--dir data/raw/run1_interfered]
"""
import csv
import re
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW, PROC = ROOT / "data" / "raw", ROOT / "data" / "processed"
THRESHOLD = 25.0


def load_cpu_log(path):
    with path.open(newline="") as f:
        rows = list(csv.reader(f))
    cols = {}
    for j, h in enumerate(rows[0][1:], 1):
        m = re.search(r"Processor\((\d+)\)", h)
        if m:
            cols[int(m.group(1))] = j
    samples = []
    for r in rows[1:]:
        if any(not r[j].strip() for j in cols.values()):
            continue  # blank first sample
        t = datetime.strptime(r[0], "%m/%d/%Y %H:%M:%S.%f").timestamp()  # local time
        samples.append((t, {c: float(r[j]) for c, j in cols.items()}))
    return samples


def bench_cpus(row):
    tool = row["tool"]
    if tool == "bw":
        return {int(c) for c in row["cpus"].split()}
    if tool == "loaded":
        cpus = {int(row["chase_cpu"])}
        cpus |= {int(c) for c in row["traffic_cpus"].split()}
        return cpus
    return {int(row["cpu"])}


def worst_other(row, samples):
    """Busiest non-benchmark CPU (mean % busy) over the row's time window.
    Shared by remeasure_flagged.py so both apply exactly the same rule."""
    t0, t1 = float(row["t_start"]), float(row["t_end"])
    win = [s for t, s in samples if t0 < t <= t1 + 1.0]
    nwin = len(win)  # 0 = no sample inside the window: nearest sample used
    if not win:
        win = [min(samples, key=lambda ts: abs(ts[0] - t1))[1]]
    mean = {c: sum(s[c] for s in win) / len(win) for c in win[0]}
    used = bench_cpus(row)
    if -1 in used:  # unpinned: busiest CPU is the benchmark
        used = {max(mean, key=mean.get)}
    others = {c: v for c, v in mean.items() if c not in used}
    cpu = max(others, key=others.get)
    return others[cpu], cpu, nwin, used


def main():
    global RAW, PROC
    suffix = sys.argv[sys.argv.index("--suffix") + 1] if "--suffix" in sys.argv else ""
    if "--dir" in sys.argv:  # e.g. --dir data/raw/run1_interfered: read and write there
        RAW = PROC = (ROOT / sys.argv[sys.argv.index("--dir") + 1]).resolve()
    # The sweep's log plus any re-measurement logs: every row is judged by the
    # samples from its own time window.
    logs = [RAW / f"cpu_log{suffix}.csv"]
    if not suffix:
        logs += sorted(RAW.glob("cpu_log_remeasure_r*.csv"))
    samples = sorted(s for p in logs if p.exists() for s in load_cpu_log(p))
    if not samples:
        print("FAIL: no CPU samples")
        return 1
    out, flagged, total, nowin = [], 0, 0, 0
    for path in sorted(RAW.glob(f"sweep_*{suffix}.csv")):
        if not suffix and path.stem.endswith("_quick"):
            continue
        name = path.stem[len("sweep_"):]
        if suffix:
            name = name[: -len(suffix)]
        with path.open(newline="") as f:
            for i, row in enumerate(csv.DictReader(f), 1):
                total += 1
                t0, t1 = float(row["t_start"]), float(row["t_end"])
                worst, worst_cpu, nwin, used = worst_other(row, samples)
                nowin += nwin == 0
                flag = worst > THRESHOLD
                flagged += flag
                out.append([name, i, row["rep"], f"{t0:.3f}", f"{t1 - t0:.2f}", nwin,
                            " ".join(map(str, sorted(used))), worst_cpu, f"{worst:.1f}", int(flag)])
    PROC.mkdir(parents=True, exist_ok=True)
    dst = PROC / f"interference{suffix}.csv"
    with dst.open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["sweep", "row", "rep", "t_start", "seconds", "samples", "bench_cpus",
                    "worst_other_cpu", "worst_other_busy_pct", "flagged"])
        w.writerows(out)
    print(f"{total} rows checked, {flagged} flagged (> {THRESHOLD:.0f}% on a non-benchmark CPU), "
          f"{nowin} short rows matched to nearest sample -> {dst.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
