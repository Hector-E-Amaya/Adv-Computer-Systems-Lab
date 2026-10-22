"""Parse fio JSON results (data/raw/<sweep>/*.json) into one CSV per sweep
(data/processed/<sweep>.csv), one row per run.

Columns: run parameters (from the runner's _meta), total IOPS / MB/s, mean
and percentile latency (total = submission + completion, per direction),
submission latency, effective queue depth by Little's Law (IOPS x mean
latency), fio's own mean outstanding depth (from iodepth_level), and CPU use.
Usage: python scripts/parse_fio.py [sweep ...]   (default: all sweeps)
"""
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW, PROC = ROOT / "data" / "raw", ROOT / "data" / "processed"
PCTS = [("p50", "50.000000"), ("p95", "95.000000"), ("p99", "99.000000"), ("p999", "99.900000")]
PARAMS = ["rw", "bs", "iodepth", "numjobs", "rwmixread", "ioengine", "direct", "size", "runtime", "filename"]


def depth_mean(levels):
    """Mean outstanding depth from fio's iodepth_level histogram (% of time
    at depth bins 1, 2, 4, 8, 16, 32, >=64), using each bin's lower edge, so
    this is a lower bound."""
    tot = 0.0
    for k, pct in levels.items():
        d = 64 if k.startswith(">=") else int(k)
        tot += d * pct / 100
    return tot


def rows_for(path):
    try:
        data = json.loads(path.read_text(errors="replace"))
    except json.JSONDecodeError as e:
        raise SystemExit(f"FAIL: {path} is not valid fio JSON ({e}); failed runs must be *.json.failed")
    meta = data.get("_meta", {})
    p = meta.get("params", {})
    j = data["jobs"][0]
    out = {"sweep": meta.get("sweep", path.parent.name), "name": meta.get("name", path.stem),
           "rep": meta.get("rep", ""), "t_start": round(meta.get("t_start", 0), 3)}
    for k in PARAMS:
        out[k] = p.get(k, "")
    if out["numjobs"] == "":
        out["numjobs"] = 1
    io = {d: j[d] for d in ("read", "write") if j[d]["io_bytes"] > 0}
    iops = sum(x["iops"] for x in io.values())
    out["iops"] = round(iops, 1)
    out["mbps"] = round(sum(x["bw_bytes"] for x in io.values()) / 1e6, 2)
    lat_mean = sum(x["iops"] * x["lat_ns"]["mean"] for x in io.values()) / iops if iops else 0
    out["lat_mean_us"] = round(lat_mean / 1e3, 2)
    for d in ("read", "write"):
        x = io.get(d)
        out[f"{d}_iops"] = round(x["iops"], 1) if x else ""
        out[f"{d}_lat_mean_us"] = round(x["lat_ns"]["mean"] / 1e3, 2) if x else ""
        out[f"{d}_slat_mean_us"] = round(x["slat_ns"]["mean"] / 1e3, 2) if x else ""
        for name, key in PCTS:
            v = x["lat_ns"].get("percentile", {}).get(key) if x else None
            out[f"{d}_{name}_us"] = round(v / 1e3, 2) if v else ""
    out["requested_qd"] = int(p.get("iodepth", 1)) * int(out["numjobs"])
    out["littles_qd"] = round(iops * lat_mean / 1e9, 2)
    out["fio_depth_mean_lb"] = round(depth_mean(j.get("iodepth_level", {})) * int(out["numjobs"]), 2)
    out["usr_cpu"], out["sys_cpu"] = round(j["usr_cpu"], 1), round(j["sys_cpu"], 1)
    return out


def main(sweeps):
    PROC.mkdir(parents=True, exist_ok=True)
    dirs = [RAW / s for s in sweeps] if sweeps else sorted(d for d in RAW.iterdir() if d.is_dir())
    for d in dirs:
        files = sorted(d.glob("*.json"))
        if not files:
            continue
        rows = [rows_for(f) for f in files]
        keys = list(rows[0])
        with open(PROC / f"{d.name}.csv", "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=keys, lineterminator="\n")
            w.writeheader()
            w.writerows(rows)
        print(f"{d.name}: {len(rows)} runs -> data/processed/{d.name}.csv")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
