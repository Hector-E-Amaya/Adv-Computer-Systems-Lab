"""Safe fio runner: every run gets a job file in configs/, safety checks, and
JSON output in data/raw/<sweep>/.

Safety (reports/SAFETY.md), checked before EVERY run and refused otherwise:
  - target file must be inside C:\\fio-test\\ (no raw devices, no drive letters);
    fio options that pick files another way (directory, opendir,
    filename_format) are refused
  - direct=1, unless the run is the declared read-only buffered pitfall
  - creating/extending the file must leave >= 100 GB free on C:

Library use: run(sweep, name, rep, **params). CLI: see main() / --help.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIO = os.environ.get("FIO", r"C:\Program Files\fio\fio.exe")
TESTDIR = Path(r"C:\fio-test")
MAIN_FILE = TESTDIR / "testfile_32g.dat"
MAIN_SIZE = 32 << 30
FREE_FLOOR = 100 * 10**9  # bytes that must remain free on C:

BASE = {  # applied to every job unless overridden
    "ioengine": "windowsaio",
    "direct": 1,
    "thread": 1,
    "time_based": 1,
    "runtime": 15,
    "ramp_time": 3,
    "randrepeat": 0,
    "norandommap": 1,
    "refill_buffers": 1,           # fresh random data per write: incompressible
    "buffer_compress_percentage": 0,
    "lat_percentiles": 1,          # percentiles on total latency (submit + complete)
    "percentile_list": "50:90:95:99:99.9:99.99",
    "group_reporting": 1,
}


class SafetyError(Exception):
    pass


def check_safety(p):
    fn = str(p.get("filename", ""))
    if not fn:
        raise SafetyError("no filename")
    for k in ("directory", "opendir", "filename_format"):
        if k in p:
            raise SafetyError(f"'{k}' can direct I/O outside {TESTDIR}; refused")
    if re.search(r"(?i)physicaldrive|^\\\\\.\\|^[a-z]:\\?$", fn):
        raise SafetyError(f"raw device / bare drive refused: {fn}")
    path = Path(fn).resolve()
    if TESTDIR.resolve() not in path.parents:
        raise SafetyError(f"target outside {TESTDIR}: {fn}")
    if int(p.get("direct", 0)) != 1:
        if not (p.get("_buffered_pitfall") and p.get("rw") in ("read", "randread")):
            raise SafetyError("direct=0 is only allowed for the declared read-only buffered pitfall")
    size = int(p.get("size", 0))
    have = path.stat().st_size if path.exists() else 0
    grow = max(0, size - have)
    free = shutil.disk_usage(str(TESTDIR.anchor)).free
    if free - grow < FREE_FLOOR:
        raise SafetyError(f"would leave {(free - grow) / 1e9:.0f} GB free (< {FREE_FLOOR / 1e9:.0f} GB floor)")


def job_text(name, p):
    """fio job file. Colons in Windows paths must be escaped as '\\:'."""
    lines = ["[global]"]
    for k, v in {**BASE, **p}.items():
        if k.startswith("_"):
            continue
        if k == "filename":
            # fio treats ':' in filename as a file separator. Log paths
            # (write_*_log) must NOT be escaped: fio then fails to open them.
            v = str(v).replace(":", "\\:")
        lines.append(f"{k}={v}")
    lines += ["", f"[{name}]", ""]
    return "\n".join(lines)


def run(sweep, name, rep, quiet=False, **params):
    """Run one fio job; returns the parsed JSON dict (and saves it)."""
    p = {"filename": str(MAIN_FILE), "size": MAIN_SIZE, **params}
    check_safety({**BASE, **p})
    TESTDIR.mkdir(exist_ok=True)
    cfg_dir, out_dir = ROOT / "configs" / sweep, ROOT / "data" / "raw" / sweep
    cfg_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    cfg = cfg_dir / f"{name}.fio"
    cfg.write_text(job_text(name, p), newline="\n")
    out = out_dir / f"{name}_r{rep}.json"
    t0 = time.time()
    r = subprocess.run([FIO, "--output-format=json", f"--output={out}", str(cfg)],
                       capture_output=True, text=True)
    if r.returncode != 0 or not out.exists():
        if out.exists():  # keep the evidence, but never where the parser reads data
            out.replace(out.with_suffix(".json.failed"))
        raise RuntimeError(f"fio failed ({r.returncode}) for {name}: {r.stderr.strip()[:300]}")
    text = out.read_text(errors="replace")
    data = json.loads(text[text.find("{"):])  # fio may print notes before the JSON
    data["_meta"] = {"sweep": sweep, "name": name, "rep": rep, "t_start": t0, "t_end": time.time(),
                     "params": {k: v for k, v in {**BASE, **p}.items() if not k.startswith("_")}}
    out.write_text(json.dumps(data, indent=1), newline="\n")
    if not quiet:
        s = summary(data)
        print(f"  {sweep}/{name} r{rep}: {s['iops']:.0f} IOPS, {s['mbps']:.1f} MB/s, mean {s['lat_mean_us']:.1f} us, "
              f"p99 {s['p99_us']:.1f} us, LL QD {s['littles_qd']:.2f}")
    return data


def summary(data):
    """Aggregate read+write of the (group-reported) job."""
    j = data["jobs"][0]
    io = [j[d] for d in ("read", "write") if j[d]["io_bytes"] > 0]
    iops = sum(x["iops"] for x in io)
    bw = sum(x["bw_bytes"] for x in io)
    # IOPS-weighted mean latency over directions
    lat_mean = sum(x["iops"] * x["lat_ns"]["mean"] for x in io) / iops if iops else 0.0
    pct = {}
    for key in ("50.000000", "95.000000", "99.000000", "99.900000"):
        pct[key] = max(x["lat_ns"].get("percentile", {}).get(key, 0) for x in io)
    return {"iops": iops, "mbps": bw / 1e6, "lat_mean_us": lat_mean / 1e3,
            "p50_us": pct["50.000000"] / 1e3, "p95_us": pct["95.000000"] / 1e3,
            "p99_us": pct["99.000000"] / 1e3, "p999_us": pct["99.900000"] / 1e3,
            "littles_qd": iops * lat_mean / 1e9}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest-safety", action="store_true", help="check that unsafe targets are refused")
    a = ap.parse_args()
    if a.selftest_safety:
        bad = [{"filename": r"\\.\PhysicalDrive0", "direct": 1},
               {"filename": "C:", "direct": 1},
               {"filename": r"C:\Users\hamay\Documents\x.dat", "direct": 1},
               {"filename": str(TESTDIR / "x.dat"), "direct": 0, "rw": "randwrite"},
               {"filename": str(TESTDIR / "x.dat"), "direct": 1, "size": 10**15},
               {"filename": str(TESTDIR / ".." / "Users" / "x.dat"), "direct": 1},
               {"filename": str(TESTDIR) + "-evil\\x.dat", "direct": 1},
               {"filename": str(TESTDIR / "x.dat"), "direct": 1, "opendir": r"C:\Users"},
               {"filename": str(TESTDIR / "x.dat"), "direct": 1, "directory": r"C:\Users"},
               {"filename": str(TESTDIR / "x.dat"), "direct": 1, "filename_format": r"C:\Users\$jobname"}]
        good = [{"filename": str(TESTDIR / "x.dat"), "direct": 1, "size": 1 << 30},
                {"filename": str(TESTDIR / "x.dat"), "direct": 0, "rw": "randread", "_buffered_pitfall": 1,
                 "size": 1 << 30}]
        ok = True
        for p in bad:
            try:
                check_safety(p)
                print("FAIL (not refused):", p)
                ok = False
            except SafetyError as e:
                print("PASS refused:", e)
        for p in good:
            try:
                check_safety(p)
                print("PASS allowed:", p["filename"], "direct", p["direct"])
            except SafetyError as e:
                print("FAIL (refused a safe run):", e)
                ok = False
        return 0 if ok else 1
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
