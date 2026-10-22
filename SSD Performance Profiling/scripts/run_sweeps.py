"""Batch 2 fio sweeps (no administrator rights needed).

  python scripts/run_sweeps.py [prep qd1 bs mix qd tail state pitfall | all]
  QUICK=1 python scripts/run_sweeps.py all   # smoke test: 1 GiB file, 2 s runs, 1 rep

prep     fill the 16 GiB test file once with incompressible data (sequential
         1 MiB writes), so every read hits written, mapped LBAs (reading
         never-written LBAs is a known pitfall: the SSD returns zeros without
         touching NAND)
qd1      zero-queue: 4 KiB random read/write, 128 KiB sequential read/write
bs       block-size sweep 4 KiB - 1 MiB x {randread, randwrite, read, write}
         at fixed total QD 16 (4 jobs x iodepth 4)
mix      4 KiB random, rwmixread 100/70/50/0, QD 16
qd       4 KiB randread and randwrite, total QD 1..256 (numjobs <= 4 x
         iodepth); one fio thread is submission-bound at ~55 K IOPS
         (Batch 1), so depth is scaled with jobs and the effective QD is
         reported (Little's Law)
tail     30 s runs at mid QD (16) and at the knee QD, which is computed from
         the qd sweep (smallest QD reaching >= 90% of max IOPS)
state    (a) 16 GiB sequential write to a separate file with a 1 s bandwidth
         log (SLC cache exhaustion); (b) 120 s 4 KiB random write at QD 16
         with a 1 s IOPS log (burst vs steady state)
pitfall  buffered (page cache) vs direct reads; single-thread iodepth 32/64
         (requested vs effective QD)

All runs go through scripts/fio_run.py (safety checks, job files in
configs/, JSON in data/raw/). Windows disk and CPU counters (typeperf, 1 s)
are logged to data/raw/perf_log.csv during the sweeps: an independent check
of fio's IOPS and queue length, and an interference check. Repetition loop
outermost. Exit status nonzero if any run fails.
"""
import csv
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fio_run as F  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
QUICK = os.environ.get("QUICK") == "1"
REPS = 1 if QUICK else 3
SUF = "_quick" if QUICK else ""
MAIN = Path(r"C:\fio-test") / ("val_1g.dat" if QUICK else "testfile_16g.dat")
MSIZE = (1 << 30) if QUICK else (16 << 30)  # 16 GiB: >> any cache; halves the prep fill (wear)
# 8 s runs: even 4 KiB QD1 reads (~7 K IOPS) give ~56 K latency samples per run
T = dict(runtime=2, ramp_time=1) if QUICK else dict(runtime=8, ramp_time=2)
COMMON = dict(filename=str(MAIN), size=MSIZE, **T)
failures = []


WRITE_T = dict(runtime=2, ramp_time=1) if QUICK else dict(runtime=5, ramp_time=1)  # wear policy


def run(sweep, name, rep, **p):
    # Time-based write runs in the sweeps are shortened (wear policy, NOTES);
    # explicit runtimes (tail, state) are kept.
    rw = str(p.get("rw", ""))
    if ("write" in rw or rw in ("randrw", "rw")) and "runtime" not in p:
        p = {**WRITE_T, **p}
    try:
        return F.run(sweep + SUF, name, rep, **{**COMMON, **p})
    except Exception as e:  # keep going: one failed run must not lose the rest
        failures.append(f"{sweep}/{name} r{rep}: {e}")
        print("  FAILED:", failures[-1])
        return None


def sweep_prep():
    marker = MAIN.with_suffix(".filled")
    if marker.exists() and MAIN.exists() and MAIN.stat().st_size >= MSIZE:
        print("prep: test file already filled")
        return
    F.check_safety({**F.BASE, "filename": str(MAIN), "size": MSIZE, "direct": 1})
    run("prep", "fill_seqwrite_1m", 1, rw="write", bs="1m", iodepth=8, time_based=0, runtime=0, ramp_time=0)
    if not failures:
        marker.write_text(time.strftime("%Y-%m-%d %H:%M:%S"))


def sweep_qd1():
    for rep in range(1, REPS + 1):
        for rw, bs in (("randread", "4k"), ("randwrite", "4k"), ("read", "128k"), ("write", "128k")):
            run("qd1", f"{rw}_{bs}_qd1", rep, rw=rw, bs=bs, iodepth=1)


def sweep_bs():
    sizes = ["4k", "128k"] if QUICK else ["4k", "16k", "32k", "64k", "128k", "256k", "512k", "1m"]
    for rep in range(1, REPS + 1):
        for bs in sizes:
            for rw in ("randread", "randwrite", "read", "write"):
                run("bs", f"{rw}_{bs}_qd16", rep, rw=rw, bs=bs, iodepth=4, numjobs=4)


def sweep_mix():
    for rep in range(1, REPS + 1):
        for mix in (100, 70, 50, 0):
            run("mix", f"randrw_4k_read{mix}_qd16", rep, rw="randrw", rwmixread=mix, bs="4k", iodepth=4, numjobs=4)


QD_POINTS = [(1, 1), (1, 2), (1, 4), (2, 4), (4, 4), (4, 8), (4, 16), (4, 32), (4, 64)]  # (jobs, depth)


def sweep_qd():
    pts = [(1, 1), (4, 4), (4, 64)] if QUICK else QD_POINTS
    for rep in range(1, REPS + 1):
        for rw in ("randread", "randwrite"):
            for jobs, depth in pts:
                run("qd", f"{rw}_4k_qd{jobs * depth}", rep, rw=rw, bs="4k", iodepth=depth, numjobs=jobs)


def knee_qd(rw):
    """Smallest total QD reaching >= 90% of the best mean IOPS in the qd sweep."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import parse_fio
    parse_fio.main(["qd" + SUF])
    by = {}
    with open(ROOT / "data" / "processed" / f"qd{SUF}.csv") as f:
        for r in csv.DictReader(f):
            if r["rw"] == rw:
                by.setdefault(int(r["requested_qd"]), []).append(float(r["iops"]))
    mean = {q: sum(v) / len(v) for q, v in by.items()}
    best = max(mean.values())
    return min(q for q, v in mean.items() if v >= 0.9 * best)


def sweep_tail():
    for rw in ("randread", "randwrite"):
        # 30 s: >= 2 M samples at QD >= 16, enough for p99.9 (reads and writes)
        t = dict(runtime=4, ramp_time=1) if QUICK else dict(runtime=30, ramp_time=3)
        k = knee_qd(rw)
        print(f"tail: {rw} knee QD = {k}" + (" (same as mid QD 16: knee run skipped)" if k == 16 else ""))
        points = [("mid", 16)] + ([("knee", k)] if k != 16 else [])
        for rep in range(1, REPS + 1):
            for label, q in points:
                jobs = 1 if q < 4 else 4
                run("tail", f"{rw}_4k_{label}_qd{q}", rep, rw=rw, bs="4k", iodepth=max(1, q // jobs),
                    numjobs=jobs, **t)


def sweep_state():
    logdir = ROOT / "data" / "raw" / ("state" + SUF)
    logdir.mkdir(parents=True, exist_ok=True)
    # 16 GiB, not 128: the write path drains at ~150 MB/s and the 1 s log
    # already shows the drop within seconds; wear on the system SSD is kept
    # low (NOTES: wear policy).
    slc = Path(r"C:\fio-test") / ("slc_quick.dat" if QUICK else "slc_test_16g.dat")
    size = (2 << 30) if QUICK else (16 << 30)
    run("state", "slc_seqwrite_1m", 1, filename=str(slc), size=size, rw="write", bs="1m", iodepth=8,
        time_based=0, runtime=0, ramp_time=0, write_bw_log=str(logdir / "slc_seqwrite_1m"), log_avg_msec=1000)
    t = dict(runtime=6, ramp_time=0) if QUICK else dict(runtime=120, ramp_time=0)
    run("state", "steady_randwrite_4k_qd16", 1, rw="randwrite", bs="4k", iodepth=4, numjobs=4,
        write_iops_log=str(logdir / "steady_randwrite_4k_qd16"), log_avg_msec=1000, **t)


def sweep_pitfall():
    small = dict(filename=r"C:\fio-test\pitfall_256m.dat", size=256 << 20)
    for rep in range(1, REPS + 1):
        run("pitfall", "direct_randread_4k", rep, rw="randread", bs="4k", iodepth=1, **small)
        buf = dict(direct=0, invalidate=0, ioengine="psync", _buffered_pitfall=1, **small)
        run("pitfall", "buffered_warm_seqread_1m", rep, rw="read", bs="1m", **buf)
        run("pitfall", "buffered_randread_4k", rep, rw="randread", bs="4k", **buf)
        for depth in (32, 64):
            run("pitfall", f"onethread_randread_4k_iodepth{depth}", rep, rw="randread", bs="4k", iodepth=depth)


def start_perf_log():
    out = ROOT / "data" / "raw" / f"perf_log{SUF}.csv"
    counters = [r"\PhysicalDisk(_Total)\Disk Transfers/sec", r"\PhysicalDisk(_Total)\Disk Bytes/sec",
                r"\PhysicalDisk(_Total)\Avg. Disk Queue Length", r"\Processor(_Total)\% Processor Time"]
    p = subprocess.Popen(["typeperf", *counters, "-si", "1", "-f", "CSV", "-o", str(out), "-y"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    if p.poll() is not None:
        raise SystemExit("FAIL: typeperf did not start")
    return p


def main(args):
    order = ["prep", "qd1", "bs", "mix", "qd", "tail", "state", "pitfall"]
    todo = order if not args or args == ["all"] else args
    for s in todo:
        if s not in order:
            raise SystemExit(f"unknown sweep {s}")
    if "prep" not in todo and not MAIN.with_suffix(".filled").exists():
        raise SystemExit("run 'prep' first: the test file must be filled before measuring")
    print(f"sweeps {todo} QUICK={QUICK} REPS={REPS} file={MAIN} started {time.strftime('%H:%M:%S')}")
    log = start_perf_log()
    try:
        for s in todo:
            print(f"== {s} ({time.strftime('%H:%M:%S')})")
            globals()["sweep_" + s]()
    except KeyboardInterrupt:
        print("INTERRUPTED: sweeps stopped before finishing; results are incomplete.")
        failures.append("interrupted")
    finally:
        log.terminate()
        log.wait()
    print(f"finished {time.strftime('%H:%M:%S')}; failed runs: {len(failures)}")
    for f in failures:
        print("  ", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
