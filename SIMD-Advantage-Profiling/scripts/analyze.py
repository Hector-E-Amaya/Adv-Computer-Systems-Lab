"""Aggregate the sweeps, check the hypotheses, draw the figures.

Inputs  data/raw/sweep_{size,align,tail,stride}.csv, data/raw/peaks.csv (optional)
Outputs data/processed/*.csv, plots/*.png, reports/RESULTS_TABLES.md

Statistics: each bench row is already the median of 7 timed trials. Across the
REPS full repetitions we take the median, and report min..max as the spread.
Speedup is computed per repetition (scalar rep r / build rep r), then summarized,
so slow drift that hits both builds of a repetition cancels.
"""
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RAW, PROC, PLOTS, REPORTS = ROOT / "data/raw", ROOT / "data/processed", ROOT / "plots", ROOT / "reports"

BUILDS = ["scalar", "sse2", "avx2", "avx512"]
KERNELS = ["saxpy", "mul", "stencil", "dot", "dot_simd"]
NBUF = {"saxpy": 2, "mul": 3, "stencil": 2, "dot": 2, "dot_simd": 2}  # arrays allocated
ESIZE = {"f32": 4, "f64": 8}
CACHES = [("L1", 48 << 10), ("L2", 1280 << 10), ("L3", 8 << 20)]

# Categorical slots 1-4 of the dataviz reference palette, fixed order by build;
# markers are the secondary encoding so identity never rests on color alone.
COLOR = dict(zip(BUILDS, ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]))
MARKER = dict(zip(BUILDS, ["o", "s", "^", "D"]))
SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": INK2, "axes.labelcolor": INK, "text.color": INK,
    "xtick.color": INK2, "ytick.color": INK2, "axes.grid": True, "grid.color": GRID,
    "grid.linewidth": 0.6, "axes.spines.top": False, "axes.spines.right": False,
    "lines.linewidth": 1.6, "lines.markersize": 4, "font.size": 9, "legend.frameon": False,
    "figure.dpi": 150,
})


# ---------------------------------------------------------------- loading

def load(name):
    path = RAW / f"sweep_{name}.csv"
    if not path.exists():
        print(f"skip {name}: {path.name} missing")
        return None
    d = pd.read_csv(path)
    bad = d[d.valid != 1]
    if len(bad):
        print(f"WARNING {name}: {len(bad)} rows failed validation; excluded")
        d = d[d.valid == 1]
    d["footprint"] = d.n * d.stride * d.kernel.map(NBUF) * d.dtype.map(ESIZE)
    return d


def regime(fp):
    if fp <= 40 << 10:
        return "L1"
    if fp <= 1 << 20:
        return "L2"
    if fp <= 6 << 20:
        return "L3"
    if fp >= 32 << 20:
        return "DRAM"
    return "transition"


def summarize(d, keys):
    """Median over reps of the per-row metrics, spread of t_med across reps."""
    g = d.groupby(keys + ["build"])
    s = g.agg(t_ns=("t_med_ns", "median"), t_lo=("t_med_ns", "min"), t_hi=("t_med_ns", "max"),
              gflops=("gflops", "median"), gf_lo=("gflops", "min"), gf_hi=("gflops", "max"),
              gbps=("gbps", "median"), bw_lo=("gbps", "min"), bw_hi=("gbps", "max"),
              cpe=("cycles_per_elem", "median"), ns_per_elem=("ns_per_elem", "median"),
              ghz=("ghz_before", "median"), reps=("rep", "nunique")).reset_index()
    s["cv_pct"] = 100 * g.t_med_ns.std().values / s.t_ns
    return s


def speedups(d, keys, base="scalar"):
    """Per-rep speedup vs `base`, then median / min / max over reps."""
    p = d.pivot_table(index=keys + ["rep"], columns="build", values="t_med_ns")
    rows = []
    for b in BUILDS:
        if b in p:
            sp = (p[base] / p[b]).rename("sp").reset_index()
            sp["build"] = b
            rows.append(sp)
    s = pd.concat(rows).groupby(keys + ["build"]).sp.agg(["median", "min", "max"]).reset_index()
    return s.rename(columns={"median": "speedup", "min": "sp_lo", "max": "sp_hi"})


# ---------------------------------------------------------------- plotting helpers

def cache_lines(ax, kernel, dtype, stride=1):
    per_elem = NBUF[kernel] * ESIZE[dtype] * stride
    for name, cap in CACHES:
        x = cap / per_elem
        ax.axvline(x, color=INK2, lw=0.8, ls=":", zorder=0)
        ax.text(x, 1.0, f" {name}", transform=ax.get_xaxis_transform(), color=INK2,
                fontsize=7, va="top", ha="left")


def build_lines(ax, s, x, y, lo=None, hi=None, builds=BUILDS, **kw):
    for b in builds:
        t = s[s.build == b].sort_values(x)
        if t.empty:
            continue
        ax.plot(t[x], t[y], color=COLOR[b], marker=MARKER[b], label=b, **kw)
        if lo and hi:
            ax.fill_between(t[x], t[lo], t[hi], color=COLOR[b], alpha=0.15, lw=0)


def legend_panel(fig, ax):
    handles, labels = ax.get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=len(labels), bbox_to_anchor=(0.5, 1.0))


def save(fig, name):
    PLOTS.mkdir(exist_ok=True)
    fig.savefig(PLOTS / name, bbox_inches="tight")
    plt.close(fig)
    print(f"  plots/{name}")


# ---------------------------------------------------------------- sweeps

def size_sweep(d, out):
    keys = ["kernel", "dtype", "n"]
    s = summarize(d, keys).merge(speedups(d, keys), on=keys + ["build"])
    s["footprint"] = s.n * s.kernel.map(NBUF) * s.dtype.map(ESIZE)
    s["regime"] = s.footprint.map(regime)
    s.to_csv(PROC / "size_summary.csv", index=False, lineterminator="\n")

    for dt in ["f32", "f64"]:
        for metric, ylabel, fname, logy in [("gflops", "GFLOP/s", "size_gflops", True),
                                            ("speedup", "speedup vs scalar", "size_speedup", False)]:
            fig, axes = plt.subplots(2, 3, figsize=(11, 6.2))
            for ax, k in zip(axes.flat, KERNELS):
                t = s[(s.kernel == k) & (s.dtype == dt)]
                if metric == "speedup":
                    build_lines(ax, t, "n", "speedup", "sp_lo", "sp_hi")
                    ax.axhline(1, color=INK2, lw=0.8)
                else:
                    build_lines(ax, t, "n", "gflops", "gf_lo", "gf_hi")
                    ax.set_yscale("log")
                ax.set_xscale("log", base=2)
                cache_lines(ax, k, dt)
                ax.set_title(f"{k} ({dt})", loc="left", fontsize=9)
                ax.set_xlabel("N (elements)")
                ax.set_ylabel(ylabel)
            axes.flat[-1].axis("off")
            legend_panel(fig, axes.flat[0])
            fig.suptitle(f"{ylabel} vs problem size, {dt} (median of reps; band = min..max)",
                         y=1.04, fontsize=10)
            fig.tight_layout()
            save(fig, f"{fname}_{dt}.png")

    # fp64 vs fp32 element throughput (H4)
    p = s.pivot_table(index=["kernel", "n", "build"], columns="dtype", values="t_ns").reset_index()
    p["ratio"] = p.f32 / p.f64  # f64 elements/s relative to f32
    fig, axes = plt.subplots(1, 3, figsize=(11, 3.2))
    for ax, k in zip(axes, ["saxpy", "mul", "stencil"]):
        build_lines(ax, p[p.kernel == k], "n", "ratio")
        ax.axhline(0.5, color=INK2, lw=0.8, ls="--")
        ax.set_xscale("log", base=2)
        cache_lines(ax, k, "f32")
        ax.set_ylim(0, 1.2)
        ax.set_title(f"{k}: f64 / f32 element rate", loc="left", fontsize=9)
        ax.set_xlabel("N (elements)")
    legend_panel(fig, axes[0])
    fig.tight_layout()
    save(fig, "dtype_ratio.png")

    # negative case (H8): plain dot vs omp-simd dot
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.4), sharey=True)
    for ax, dt in zip(axes, ["f32", "f64"]):
        for k, ls in [("dot", "--"), ("dot_simd", "-")]:
            t = s[(s.kernel == k) & (s.dtype == dt) & (s.build != "scalar")]
            for b in BUILDS[1:]:
                u = t[t.build == b].sort_values("n")
                ax.plot(u.n, u.speedup, color=COLOR[b], marker=MARKER[b], ls=ls,
                        label=f"{b} {k}")
        ax.axhline(1, color=INK2, lw=0.8)
        ax.set_xscale("log", base=2)
        cache_lines(ax, "dot", dt)
        ax.set_title(f"dot product speedup vs scalar ({dt}); dashed = plain, solid = omp simd",
                     loc="left", fontsize=9)
        ax.set_xlabel("N (elements)")
    axes[0].set_ylabel("speedup vs scalar")
    axes[1].legend(fontsize=7, ncol=2)
    fig.tight_layout()
    save(fig, "dot_negative.png")

    locality_figure(s)

    # regime table: median over the points inside each regime
    r = s[s.regime != "transition"].groupby(["kernel", "dtype", "build", "regime"]).agg(
        speedup=("speedup", "median"), gflops=("gflops", "median"), cpe=("cpe", "median"),
        cv_pct=("cv_pct", "median"), points=("n", "count")).reset_index()
    r.to_csv(PROC / "size_by_regime.csv", index=False, lineterminator="\n")
    out["size"] = s
    out["regime"] = r


def locality_figure(s, kernel="saxpy", dtype="f32"):
    """One kernel across the hierarchy, annotated with where the SIMD gain compresses."""
    t = s[(s.kernel == kernel) & (s.dtype == dtype)]
    per_elem = NBUF[kernel] * ESIZE[dtype]
    bounds = [64, *(cap / per_elem for _, cap in CACHES), t.n.max() * 1.2]
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(9, 6.6), sharex=True,
                                  gridspec_kw={"height_ratios": [3, 2]})
    for i, name in enumerate(["L1", "L2", "L3", "DRAM"]):
        for a in (ax, ax2):
            a.axvspan(bounds[i], bounds[i + 1], color=INK2, alpha=0.05 if i % 2 else 0.0, lw=0)
        ax.text((bounds[i] * bounds[i + 1]) ** 0.5, 1.0, name, transform=ax.get_xaxis_transform(),
                ha="center", va="bottom", fontsize=8, color=INK2)
    build_lines(ax, t, "n", "gflops", "gf_lo", "gf_hi")
    build_lines(ax2, t, "n", "speedup", "sp_lo", "sp_hi")
    ax.set_yscale("log")
    ax.set_xscale("log", base=2)
    ax.set_ylabel("GFLOP/s")
    ax2.set_ylabel("speedup vs scalar")
    ax2.set_xlabel("N (elements)")
    ax2.axhline(1, color=INK2, lw=0.8)
    ax2.set_ylim(0, t.sp_hi.max() * 1.45)  # headroom for the notes above the curves

    def sp(build, reg):
        u = t[(t.build == build) & (t.regime == reg)]
        return u.speedup.median()
    notes = [
        (512, f"L1: speedup tracks lanes\n(avx512 {sp('avx512', 'L1'):.1f}x, avx2 {sp('avx2', 'L1'):.1f}x)\n"
              "~2 cycles per vector iteration\nat every width"),
        (20000, f"L2: gains compress\n(avx512 {sp('avx512', 'L2'):.1f}x)"),
        (400000, f"L3: SIMD builds tie\n(L3 bandwidth, {sp('avx2', 'L3'):.1f}x)"),
        (6e6, f"DRAM: memory-bound,\nscalar too ({sp('avx2', 'DRAM'):.2f}x)"),
    ]
    for x, text in notes:
        ax2.text(x, ax2.get_ylim()[1] * 0.97, text, ha="center", va="top", fontsize=7, color=INK)
    fig.suptitle(f"Locality sweep: {kernel} {dtype}; band = min..max over repetitions",
                 x=0.02, ha="left", fontsize=10)
    ax.legend(fontsize=7, ncol=4, loc="lower left")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, f"locality_{kernel}_{dtype}.png")


def align_sweep(d, out):
    keys = ["kernel", "dtype", "n", "misalign"]
    s = summarize(d, keys)
    base = s[s.misalign == 0][["kernel", "dtype", "n", "build", "t_ns"]].rename(columns={"t_ns": "t0"})
    s = s.merge(base, on=["kernel", "dtype", "n", "build"])
    s["rel_time"] = s.t_ns / s.t0
    s["rel_lo"], s["rel_hi"] = s.t_lo / s.t0, s.t_hi / s.t0  # rep spread, relative to aligned median
    s["regime"] = (s.n * s.kernel.map(NBUF) * s.dtype.map(ESIZE)).map(regime)
    s.to_csv(PROC / "align_summary.csv", index=False, lineterminator="\n")
    for dt in ["f32", "f64"]:
        ns = sorted(s.n.unique())
        fig, axes = plt.subplots(len(ns), 2, figsize=(9, 2.6 * len(ns)), squeeze=False)
        for i, n in enumerate(ns):
            for j, k in enumerate(["saxpy", "mul"]):
                ax = axes[i, j]
                t = s[(s.kernel == k) & (s.dtype == dt) & (s.n == n)]
                build_lines(ax, t, "misalign", "rel_time", "rel_lo", "rel_hi")
                ax.axhline(1, color=INK2, lw=0.8)
                reg = t.regime.iloc[0] if len(t) else ""
                ax.set_title(f"{k} {dt}, N={n} ({reg})", loc="left", fontsize=9)
                ax.set_xlabel("offset from 64-byte boundary (bytes)")
                ax.set_ylabel("time / aligned time")
        legend_panel(fig, axes[0, 0])
        fig.tight_layout(rect=(0, 0, 1, 0.96))
        save(fig, f"align_{dt}.png")
    out["align"] = s


def tail_sweep(d, out):
    s = summarize(d, ["kernel", "dtype", "n"])
    s.to_csv(PROC / "tail_summary.csv", index=False, lineterminator="\n")
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.6))
    build_lines(axes[0], s, "n", "t_ns", "t_lo", "t_hi")
    axes[0].set_ylabel("ns per call")
    build_lines(axes[1], s, "n", "cpe")
    axes[1].set_ylabel("cycles per element")
    axes[1].set_yscale("log")
    for ax in axes:
        ax.set_xlabel("N (elements)")
        for v in (4, 8, 16):
            ax.axvline(v, color=INK2, lw=0.6, ls=":", zorder=0)
    axes[0].set_title("saxpy f32, small N: call time (dotted: 4/8/16 = sse2/avx2/avx512 lanes)",
                      loc="left", fontsize=9)
    axes[1].set_title("cycles per element", loc="left", fontsize=9)
    legend_panel(fig, axes[0])
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    save(fig, "tail.png")
    out["tail"] = s


def stride_sweep(d, out):
    keys = ["kernel", "dtype", "n", "stride"]
    s = summarize(d, keys).merge(speedups(d, keys), on=keys + ["build"])
    s.to_csv(PROC / "stride_summary.csv", index=False, lineterminator="\n")
    ns = sorted(s.n.unique())
    fig, axes = plt.subplots(2, len(ns), figsize=(5 * len(ns), 6.4), squeeze=False)
    for j, n in enumerate(ns):
        for dt, mk in [("f32", "-"), ("f64", "--")]:
            t = s[(s.n == n) & (s.dtype == dt)]
            for b in BUILDS:
                u = t[t.build == b].sort_values("stride")
                axes[0, j].plot(u.stride, u.gbps, color=COLOR[b], marker=MARKER[b], ls=mk,
                                label=f"{b} {dt}")
                axes[0, j].fill_between(u.stride, u.bw_lo, u.bw_hi, color=COLOR[b], alpha=0.12, lw=0)
                axes[1, j].plot(u.stride, u.speedup, color=COLOR[b], marker=MARKER[b], ls=mk,
                                label=f"{b} {dt}")
                axes[1, j].fill_between(u.stride, u.sp_lo, u.sp_hi, color=COLOR[b], alpha=0.12, lw=0)
        axes[0, j].set_title(f"saxpy, N={n}: useful GB/s (solid f32, dashed f64)",
                             loc="left", fontsize=9)
        axes[0, j].set_yscale("log")
        axes[1, j].set_title(f"saxpy, N={n}: speedup vs scalar", loc="left", fontsize=9)
        axes[1, j].axhline(1, color=INK2, lw=0.8)
        for ax in axes[:, j]:
            ax.set_xscale("log", base=2)
            ax.set_xlabel("stride (elements)")
    axes[0, 0].set_ylabel("useful GB/s")
    axes[1, 0].set_ylabel("speedup vs scalar")
    axes[0, -1].legend(fontsize=7, ncol=2)
    fig.tight_layout()
    save(fig, "stride.png")
    out["stride"] = s


def roofline(out):
    path = RAW / "peaks.csv"
    if not path.exists() or "regime" not in out:
        print("skip roofline: peaks.csv or size sweep missing")
        return
    pk = pd.read_csv(path)
    comp = pk[(pk.kind == "compute") & (pk.dtype == "f32") & (pk.used_by != "-")]
    mem = pk[(pk.kind == "memory") & pk.variant.str.startswith("read_")]
    ai = {"saxpy": 2 / 12, "mul": 1 / 12, "stencil": 5 / 8, "dot": 2 / 8, "dot_simd": 2 / 8}
    kmark = {"saxpy": "o", "mul": "s", "stencil": "^", "dot": "v", "dot_simd": "D"}
    xs = np.logspace(-2, 2, 200)
    fig, ax = plt.subplots(figsize=(8, 5.4))
    top = comp.value.max()
    for _, m in mem.iterrows():
        lvl = m.variant.split("_")[1]
        ax.plot(xs, np.minimum(xs * m.value, top), color=INK2, lw=0.9,
                ls={"L1": "-", "L2": "--", "L3": "-.", "DRAM": ":"}[lvl])
        ax.text(0.011, 0.011 * m.value * 1.15, f"{lvl} read {m.value:.0f} GB/s", fontsize=7,
                color=INK2, rotation=0)
    # avx2 and avx512 peaks are nearly equal (one 512-bit FMA unit): label below vs above.
    for _, c in comp.iterrows():
        ax.axhline(c.value, color=COLOR[c.used_by], lw=1.0)
        below = c.used_by == "avx512"
        ax.text(90, c.value * (0.93 if below else 1.06), f"{c.used_by} peak {c.value:.0f} GFLOP/s",
                fontsize=7, ha="right", va="top" if below else "bottom", color=INK)
    r = out["regime"]
    for _, row in r[(r.dtype == "f32") & r.regime.isin(["L1", "DRAM"])].iterrows():
        l1 = row.regime == "L1"
        ax.scatter(ai[row.kernel], row.gflops, marker=kmark[row.kernel], s=42 if l1 else 30,
                   facecolors=COLOR[row.build] if l1 else "none",
                   edgecolors=SURFACE if l1 else COLOR[row.build], linewidths=1 if l1 else 1.4,
                   zorder=3)
    for b in BUILDS:
        ax.scatter([], [], color=COLOR[b], marker="o", label=b)
    for k, mk in kmark.items():
        ax.scatter([], [], color=INK2, marker=mk, label=k)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(0.01, 100)
    ax.set_xlabel("arithmetic intensity (FLOP / byte)")
    ax.set_ylabel("GFLOP/s")
    ax.set_title("Roofline, fp32, one core (filled = L1-resident, hollow = DRAM-resident)",
                 loc="left", fontsize=9)
    ax.legend(fontsize=7, ncol=2, loc="lower right")
    fig.tight_layout()
    save(fig, "roofline.png")


# ---------------------------------------------------------------- hypotheses

def pick(r, kernel, dtype, build, reg, col="speedup"):
    t = r[(r.kernel == kernel) & (r.dtype == dtype) & (r.build == build) & (r.regime == reg)]
    return float(t[col].iloc[0]) if len(t) else float("nan")


def verdict(v, lo, hi):
    if np.isnan(v):
        return "no data"
    return "consistent" if lo <= v <= hi else "NOT consistent"


def hypotheses(out):
    rows = []
    add = lambda h, what, v, lo, hi: rows.append((h, what, v, f"{lo:g}..{hi:g}", verdict(v, lo, hi)))
    if "regime" in out:
        r = out["regime"]
        add("H1", "saxpy f32 avx2 speedup, L1", pick(r, "saxpy", "f32", "avx2", "L1"), 4, 8)
        add("H1", "saxpy f32 sse2 speedup, L1", pick(r, "saxpy", "f32", "sse2", "L1"), 2, 4)
        add("H2", "saxpy f32 avx2 speedup, DRAM", pick(r, "saxpy", "f32", "avx2", "DRAM"), 1.1, 1.6)
        add("H2", "mul f32 avx2 speedup, DRAM", pick(r, "mul", "f32", "avx2", "DRAM"), 1.1, 1.6)
        for reg in ["L1", "DRAM"]:
            v = pick(r, "saxpy", "f64", "avx2", reg, "gflops") / pick(r, "saxpy", "f32", "avx2", reg, "gflops")
            add("H4", f"saxpy avx2 f64/f32 element rate, {reg}", v, 0.4, 0.6)
        v = pick(r, "saxpy", "f32", "avx512", "L1") / pick(r, "saxpy", "f32", "avx2", "L1")
        add("H7", "saxpy f32 avx512/avx2, L1", v, 0, 1.2)
        v = pick(r, "saxpy", "f32", "avx512", "DRAM") / pick(r, "saxpy", "f32", "avx2", "DRAM")
        add("H7", "saxpy f32 avx512/avx2, DRAM", v, 0.9, 1.1)
        for b in ["sse2", "avx2", "avx512"]:
            add("H8", f"plain dot f32 {b} speedup, L1", pick(r, "dot", "f32", b, "L1"), 0.8, 1.2)
        add("H8", "dot_simd f32 avx2 speedup, DRAM", pick(r, "dot_simd", "f32", "avx2", "DRAM"), 0.8, 1.6)
    if "align" in out:
        a = out["align"]
        for reg, (lo, hi) in [("L1", (1.0, 1.3)), ("L2", (1.0, 1.3)), ("DRAM", (0.95, 1.05))]:
            t = a[(a.regime == reg) & (a.misalign > 0) & (a.build == "avx512") & (a.dtype == "f32")]
            add("H5", f"worst misaligned/aligned time, avx512 f32, {reg}", t.rel_time.max(), lo, hi)
    if "tail" in out:
        t = out["tail"]
        for b in ["avx2", "avx512"]:
            u = t[t.build == b].set_index("n").cpe
            add("H5", f"{b} cycles/elem at N=17 vs N=128 (tail cost)", u.get(17, np.nan) / u.get(128, np.nan), 1.5, 50)
    if "stride" in out:
        s = out["stride"]
        big = s[(s.n == s.n.max()) & (s.dtype == "f32")]
        g = big[big.build == "avx2"].set_index("stride")
        add("H6", "avx2 f32 DRAM useful GB/s, stride 1 / stride 16", g.gbps.get(1) / g.gbps.get(16), 8, 20)
        add("H6", "avx2 f32 DRAM speedup at stride 2", g.speedup.get(2), 0.8, 1.2)
    t = pd.DataFrame(rows, columns=["hypothesis", "measure", "value", "predicted", "verdict"])
    t.to_csv(PROC / "hypothesis_checks.csv", index=False, lineterminator="\n")
    return t


def md_table(df, floatfmt=".2f"):
    cols = list(df.columns)
    lines = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, row in df.iterrows():
        cells = [f"{v:{floatfmt}}" if isinstance(v, float) else str(v) for v in row]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def write_report(out, checks):
    md = ["# Results tables", "",
          "Generated by `scripts/analyze.py`; do not edit by hand. Medians over repetitions;",
          "each repetition is itself the median of 7 timed trials.", ""]
    if len(checks):
        md += ["## Hypothesis checks (automatic; the verdict is only a range test)", "",
               md_table(checks), ""]
    if "regime" in out:
        r = out["regime"]
        p = r.pivot_table(index=["kernel", "dtype", "regime"], columns="build", values="speedup")
        p = p.reindex(columns=BUILDS).reset_index()
        order = {"L1": 0, "L2": 1, "L3": 2, "DRAM": 3}
        p = p.sort_values(["kernel", "dtype", "regime"], key=lambda c: c.map(order) if c.name == "regime" else c)
        md += ["## Speedup vs scalar by cache regime (median over sizes in the regime)", "",
               md_table(p), ""]
        g = r.pivot_table(index=["kernel", "dtype", "regime"], columns="build", values="gflops")
        g = g.reindex(columns=BUILDS).reset_index().sort_values(
            ["kernel", "dtype", "regime"], key=lambda c: c.map(order) if c.name == "regime" else c)
        md += ["## GFLOP/s by cache regime", "", md_table(g), ""]
        md += [f"Median run-to-run CV of time across repetitions: "
               f"{out['size'].cv_pct.median():.1f}% (95th pct {out['size'].cv_pct.quantile(0.95):.1f}%).", ""]
    (REPORTS / "RESULTS_TABLES.md").write_text("\n".join(md) + "\n", newline="\n")
    print("  reports/RESULTS_TABLES.md")


def main():
    PROC.mkdir(parents=True, exist_ok=True)
    out = {}
    for name, fn in [("size", size_sweep), ("align", align_sweep), ("tail", tail_sweep),
                     ("stride", stride_sweep)]:
        d = load(name)
        if d is not None:
            print(f"{name}: {len(d)} rows")
            fn(d, out)
    roofline(out)
    checks = hypotheses(out)
    if len(checks):
        print(checks.to_string(index=False))
    write_report(out, checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
