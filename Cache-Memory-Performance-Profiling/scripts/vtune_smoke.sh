#!/usr/bin/env bash
# VTune hardware-counter smoke test. MUST be run from an ADMINISTRATOR
# Git Bash (VTune hardware event sampling needs admin on Windows):
#   right-click Git Bash -> Run as administrator, cd to this folder, then
#   scripts/vtune_smoke.sh
# Output: data/raw/vtune_smoke.txt (VTune summary + per-event counts).
set -uo pipefail
cd "$(dirname "$0")/.."

V="/c/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe"
[ -x "$V" ] || { echo "vtune.exe not found at $V"; exit 1; }
[ -x build/kern.exe ] || { echo "run scripts/build.sh first"; exit 1; }
net session >/dev/null 2>&1 || echo "WARNING: this shell does not look elevated; collection will likely fail"

EV="CPU_CLK_UNHALTED.THREAD,INST_RETIRED.ANY,MEM_LOAD_RETIRED.L1_MISS,MEM_LOAD_RETIRED.L2_MISS"
EV="$EV,MEM_LOAD_RETIRED.L3_MISS,DTLB_LOAD_MISSES.WALK_COMPLETED"
OUT=data/raw/vtune_smoke.txt
R=build/vtune/smoke
rm -rf "$R"
mkdir -p build/vtune

{
    echo "# vtune smoke $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    "$V" --version 2>&1 | head -1
    echo "## collect: kern saxpy n=16M (3 x 64 MB = DRAM resident), 3 s"
    "$V" -collect-with runsa -knob event-config="$EV" -r "$R" -- \
        build/kern.exe --kernel saxpy --n 16M --seconds 3 2>&1
    echo "## report: hw-events by function"
    "$V" -report hw-events -r "$R" -group-by function -format csv -csv-delimiter comma 2>&1 | head -20
} > "$OUT"
echo "done -> $OUT"
tail -25 "$OUT"
