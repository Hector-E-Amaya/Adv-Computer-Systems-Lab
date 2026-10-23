"""Re-measure interference-flagged rows of the latency-sensitive sweeps.

Run 2 evidence (REPORT section 6): flags on single-thread latency rows (ws, tlb,
pinned pin) and single-thread bw rows came with measurably biased values.
Flags on rows using >= 4 logical CPUs (bw 4 threads, knee) showed no
measurable effect (flagged/unflagged ~1.00), and unpinned `pin` rows are
flagged by design (the thread migrates). Those stay as measured and are
documented.

For each selected flagged row: rebuild its exact command line from the row's
own columns, re-run it under a fresh typeperf logger, and re-check it with
the same rule as check_interference.py. If the new row is clean it replaces
the old one (same rep). Otherwise retry, up to --attempts times. Every
replaced original row is appended to data/raw/remeasured_originals.csv, so
nothing is lost.

Usage:
  python scripts/remeasure_flagged.py --dry-run   # print the commands, run nothing
  python scripts/remeasure_flagged.py [--attempts 3]
"""
import csv
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_interference as ci  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RAW, PROC = ROOT / "data" / "raw", ROOT / "data" / "processed"
EXE = {t: str(ROOT / "build" / f"{t}.exe") for t in ("lat", "bw")}


def command(sweep, row):
    """Rebuild the exact command line the sweep used for this row."""
    if row["tool"] == "lat":
        args = [EXE["lat"], "--size", row["size_bytes"], "--mode", row["mode"]]
        if row["k"] != "1":
            args += ["--k", row["k"]]
        if row["cpu"] == "-1":
            args += ["--cpu", "-1"]
        if row["pages"] == "2M":
            args += ["--large"]
        return args
    if row["tool"] == "bw":
        args = [EXE["bw"], "--size", row["size_per_thread"], "--threads", row["threads"],
                "--pattern", row["pattern"], "--wpct", row["wpct"], "--gran", row["gran"]]
        if row["pattern"] == "stride":
            args += ["--stride", row["stride"]]
        if row["nt"] == "1":
            args += ["--nt"]
        return args
    raise ValueError(f"no rebuild rule for tool {row['tool']}")


def selected(sweep, row):
    if sweep in ("ws", "tlb"):
        return True
    if sweep == "pin":
        return row["cpu"] != "-1"
    if sweep == "bw":
        return row["threads"] == "1"
    return False


def start_logger(path):
    p = subprocess.Popen(["typeperf", r"\Processor(*)\% Processor Time", "-si", "1", "-f", "CSV",
                          "-o", str(path), "-y"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    if p.poll() is not None:
        raise SystemExit("FAIL: typeperf did not start")
    return p


def main():
    dry = "--dry-run" in sys.argv
    attempts = int(sys.argv[sys.argv.index("--attempts") + 1]) if "--attempts" in sys.argv else 3
    flags = list(csv.DictReader(open(PROC / "interference.csv")))
    todo = []  # (sweep, row index 1-based)
    tables = {}
    for f in flags:
        if f["flagged"] != "1":
            continue
        sw, i = f["sweep"], int(f["row"])
        if sw not in tables:
            with open(RAW / f"sweep_{sw}.csv", newline="") as fh:
                rd = csv.DictReader(fh)
                tables[sw] = (rd.fieldnames, list(rd))
        row = tables[sw][1][i - 1]
        if selected(sw, row):
            todo.append((sw, i))
    print(f"{len(todo)} flagged rows selected for re-measurement")
    if dry:
        for sw, i in todo:
            row = tables[sw][1][i - 1]
            print(f"  {sw} row {i} rep {row['rep']}: {' '.join(Path(a).name if a.endswith('.exe') else a for a in command(sw, row))}")
        return 0

    orig_path = RAW / "remeasured_originals.csv"
    originals, replaced = [], 0
    pending = list(todo)
    for rnd in range(1, attempts + 1):
        if not pending:
            break
        # Each round: run every still-flagged row once under its own logger,
        # then judge each new row with the same rule as check_interference.py.
        log_path = RAW / f"cpu_log_remeasure_r{rnd}.csv"
        logger = start_logger(log_path)
        runs = []
        try:
            for sw, i in pending:
                names, rows = tables[sw]
                row = rows[i - 1]
                t0 = time.time()
                p = subprocess.run(command(sw, row), capture_output=True, text=True)
                t1 = time.time()
                if p.returncode != 0 or not p.stdout.strip():
                    print(f"  round {rnd}: {sw} row {i} run failed (exit {p.returncode})")
                    continue
                vals = next(csv.reader([p.stdout.strip()]))
                runs.append((sw, i, dict(zip(names, [row["rep"], f"{t0:.3f}", f"{t1:.3f}"] + vals))))
            time.sleep(2.5)  # let typeperf write the samples covering the last run
        finally:
            logger.terminate()
            logger.wait()
        samples = ci.load_cpu_log(log_path)
        still = []
        for sw, i, new in runs:
            worst = ci.worst_other(new, samples)[0]
            if worst > ci.THRESHOLD:
                still.append((sw, i))
                print(f"  round {rnd}: {sw} row {i} still flagged ({worst:.1f}%)")
                continue
            names, rows = tables[sw]
            originals.append({"sweep": sw, "row": i, "round": rnd, **rows[i - 1]})
            rows[i - 1] = new
            replaced += 1
        failed = [(sw, i) for sw, i in pending if (sw, i) not in {(r[0], r[1]) for r in runs}]
        pending = still + failed
        print(f"round {rnd}: ran {len(runs)}, replaced so far {replaced}, pending {len(pending)}")

    # Write back changed tables; keep every replaced original.
    for sw, (names, rows) in tables.items():
        with open(RAW / f"sweep_{sw}.csv", "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=names, lineterminator="\n")
            w.writeheader()
            w.writerows(rows)
    if originals:
        keys = ["sweep", "row", "round"] + sorted({k for o in originals for k in o} -
                                                  {"sweep", "row", "round"})
        if orig_path.exists():  # appending: keep the existing header's columns
            with open(orig_path, newline="") as fh:
                keys = next(csv.reader(fh))
        with open(orig_path, "a", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=keys, lineterminator="\n", extrasaction="ignore")
            if fh.tell() == 0:
                w.writeheader()
            w.writerows(originals)
    print(f"selected {len(todo)}, replaced {replaced} with clean re-measurements, "
          f"still flagged or failed {len(pending)}")
    for sw, i in pending:
        print(f"  not replaced: {sw} row {i}")
    return 0 if not pending else 1


if __name__ == "__main__":
    sys.exit(main())
