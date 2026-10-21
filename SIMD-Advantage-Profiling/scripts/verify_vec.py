"""Vectorization evidence for every build x kernel function.

Reads build/asm/kernels_<build>.s and build/vecinfo/kernels_<build>.txt (both
written by scripts/build.sh) and writes:
  data/processed/vectorization.csv
  reports/VECTORIZATION.md
Exit status is nonzero if an expectation fails (scalar build vectorized,
SIMD build missing its vector width, plain dot reassociated).
"""
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILDS = ["scalar", "sse2", "avx2", "avx512"]
EXPECTED_REG = {"scalar": None, "sse2": "xmm", "avx2": "ymm", "avx512": "zmm"}
EXPECTED_BYTES = {"scalar": 0, "sse2": 16, "avx2": 32, "avx512": 64}

FP_OP = r"(?:add|sub|mul|div|min|max)"
PACKED = re.compile(rf"^\s+(v?{FP_OP}p[sd]|vfn?m(?:add|sub)(?:132|213|231)p[sd])\b(.*)")
SCALAR = re.compile(rf"^\s+(v?{FP_OP}s[sd]|vfn?m(?:add|sub)(?:132|213|231)s[sd])\b")
GATHER = re.compile(r"^\s+v(?:p?gather|p?scatter)")
FUNC_DEF = re.compile(r"^\w[\w ]*?\b(\w+_f(?:32|64))\(")
VEC_MSG = re.compile(r"kernels\.c:(\d+):\d+: optimized: loop vectorized using (\d+) byte vectors")


def function_lines(src):
    """Map source line -> kernel function name, from definition order."""
    starts = []
    for i, line in enumerate(src.read_text().splitlines(), 1):
        m = FUNC_DEF.match(line)
        if m:
            starts.append((i, m.group(1)))
    def lookup(line):
        name = None
        for start, fn in starts:
            if start <= line:
                name = fn
        return name
    return [fn for _, fn in starts], lookup


def asm_functions(path):
    """Split an assembly file into {function: [instruction lines]}."""
    funcs, cur = {}, None
    for line in path.read_text().splitlines():
        m = re.match(r"^(\w+_f(?:32|64)):$", line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
        elif cur and ".seh_endproc" in line:
            cur = None
        elif cur:
            funcs[cur].append(line)
    return funcs


def analyze(lines):
    packed = {"xmm": 0, "ymm": 0, "zmm": 0}
    scalar = fma = gather = 0
    packed_add = packed_mul = 0
    for line in lines:
        m = PACKED.match(line)
        if m:
            op, operands = m.group(1), m.group(2)
            reg = "zmm" if "%zmm" in operands else "ymm" if "%ymm" in operands else "xmm"
            packed[reg] += 1
            if "fm" in op:
                fma += 1
                packed_add += 1
                packed_mul += 1
            elif "add" in op or "sub" in op:
                packed_add += 1
            elif "mul" in op:
                packed_mul += 1
        elif SCALAR.match(line):
            scalar += 1
            if "fm" in line.split()[0]:
                fma += 1
        if GATHER.match(line):
            gather += 1
    widest = next((r for r in ("zmm", "ymm", "xmm") if packed[r]), "none")
    return packed, scalar, fma, gather, packed_add, packed_mul, widest


def reduction_kind(fn, packed_add, packed_mul, scalar):
    if not fn.startswith("dot"):
        return ""
    if packed_add:
        return "reassociated (packed partial sums)"
    if packed_mul and scalar:
        return "in-order (packed multiply + serial scalar adds)"
    return "scalar"


def main():
    order, line_to_fn = function_lines(ROOT / "src" / "kernels.c")
    rows, problems = [], []
    for b in BUILDS:
        funcs = asm_functions(ROOT / "build" / "asm" / f"kernels_{b}.s")
        vec_bytes = {}
        for line in (ROOT / "build" / "vecinfo" / f"kernels_{b}.txt").read_text().splitlines():
            m = VEC_MSG.search(line)
            if m:  # first message per loop is the main loop, later ones are epilogues
                fn = line_to_fn(int(m.group(1)))
                vec_bytes.setdefault(fn, int(m.group(2)))
        for fn in order:
            packed, scalar, fma, gather, padd, pmul, widest = analyze(funcs.get(fn, []))
            elem = 4 if fn.endswith("f32") else 8
            vb = vec_bytes.get(fn, 0)
            row = {
                "build": b, "function": fn,
                "gcc_vectorized": "yes" if vb else "no",
                "vector_bytes": vb, "lanes": vb // elem if vb else 1,
                "widest_reg": widest,
                "packed_xmm": packed["xmm"], "packed_ymm": packed["ymm"], "packed_zmm": packed["zmm"],
                "scalar_fp": scalar, "fma": fma, "gather_scatter": gather,
                "reduction": reduction_kind(fn, padd, pmul, scalar),
            }
            rows.append(row)

            if b == "scalar" and (vb or widest != "none"):
                problems.append(f"{b}/{fn}: scalar build contains vector code")
            if b != "scalar" and not fn.startswith("saxpy_strided"):
                if vb != EXPECTED_BYTES[b] or widest != EXPECTED_REG[b]:
                    problems.append(f"{b}/{fn}: expected {EXPECTED_BYTES[b]}B/{EXPECTED_REG[b]}, "
                                    f"got {vb}B/{widest}")
            if fn.startswith("dot_f") and padd:
                problems.append(f"{b}/{fn}: plain dot was reassociated (would violate strict FP)")

    out_csv = ROOT / "data" / "processed" / "vectorization.csv"
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        w.writeheader()
        w.writerows(rows)

    md = ["# Vectorization verification", "",
          "Generated by `scripts/verify_vec.py` from the gcc `-fopt-info-vec` reports and the",
          "kernel assembly (`build/asm/`). Packed counts are static instruction counts in each",
          "function (main loop and epilogues), not dynamic counts.", "",
          "`saxpy_strided_*`: gcc versions this loop on `stride == 1` (`cmpq $1, %rdx; jne`).",
          "The packed instructions counted here belong to that unit-stride copy only; any",
          "stride > 1 runs a scalar loop in every build. The compiler's \"vectorized\" report",
          "does not apply to the strided case.", "",
          "| build | function | gcc vectorized | bytes | lanes | widest reg | packed x/y/z | scalar FP | FMA | gather | reduction |",
          "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        md.append(f"| {r['build']} | {r['function']} | {r['gcc_vectorized']} | {r['vector_bytes']} | "
                  f"{r['lanes']} | {r['widest_reg']} | {r['packed_xmm']}/{r['packed_ymm']}/{r['packed_zmm']} | "
                  f"{r['scalar_fp']} | {r['fma']} | {r['gather_scatter']} | {r['reduction']} |")
    md += ["", "## Checks", ""]
    md += [f"- FAIL: {p}" for p in problems] or ["- All expectations met: scalar build has no vector code; "
                                                 "each SIMD build uses its full width for every unit-stride "
                                                 "kernel; the plain dot product keeps strict FP order."]
    (ROOT / "reports" / "VECTORIZATION.md").write_text("\n".join(md) + "\n")

    for p in problems:
        print("FAIL", p)
    print(f"{len(rows)} rows -> {out_csv.relative_to(ROOT)}, reports/VECTORIZATION.md; problems={len(problems)}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
