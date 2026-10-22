#!/usr/bin/env bash
# VTune smoke test: checks, before any real counter run, that
#   1. every hardware event we plan to use exists and counts on this CPU
#      (saxpy, DRAM-resident),
#   2. DTLB page-walk counting works (lat page mode, 8192 pages),
#   3. VTune memory-access analysis can measure DRAM bandwidth (bw seq read).
#
# MUST be run from an ADMINISTRATOR Git Bash (VTune hardware sampling needs
# admin on Windows):
#   Start menu -> Git Bash (right-click) -> Run as administrator, then
#   cd "/c/Users/hamay/Documents/AdvCompSys Project/Adv-Computer-Systems-Lab/Cache-Memory-Performance-Profiling"
#   scripts/vtune_smoke.sh
# Output: data/raw/vtune_smoke.txt. Takes about 1-2 minutes.
set -uo pipefail
cd "$(dirname "$0")/.."

V="/c/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe"
[ -x "$V" ] || { echo "vtune.exe not found at $V"; exit 1; }
for t in kern lat bw; do [ -x build/$t.exe ] || { echo "run scripts/build.sh first"; exit 1; }; done
net session >/dev/null 2>&1 || echo "WARNING: this shell does not look elevated; collection will likely fail"

# Fixed counters (cycles, instructions) + 7 programmable events.
EV="CPU_CLK_UNHALTED.THREAD,INST_RETIRED.ANY"
EV="$EV,MEM_LOAD_RETIRED.L1_HIT,MEM_LOAD_RETIRED.L2_HIT,MEM_LOAD_RETIRED.L3_HIT,MEM_LOAD_RETIRED.L3_MISS"
EV="$EV,L2_LINES_IN.ALL,LONGEST_LAT_CACHE.MISS,DTLB_LOAD_MISSES.WALK_COMPLETED"
OUT=data/raw/vtune_smoke.txt
mkdir -p build/vtune

collect_events() { # name, then the command to profile
    local name=$1 r=build/vtune/smoke_$1
    shift
    rm -rf "$r"
    echo "## collect events: $name: $*"
    "$V" -collect-with runsa -knob event-config="$EV" -r "$r" -- "$@" 2>&1
    echo "## report: $name"
    "$V" -report hw-events -r "$r" -group-by process -format csv -csv-delimiter comma 2>&1 | head -10
}

{
    echo "# vtune smoke $(date -u +%Y-%m-%dT%H:%M:%SZ), commit $(git rev-parse --short HEAD)"
    "$V" --version 2>&1 | head -1
    collect_events saxpy build/kern.exe --kernel saxpy --n 16M --seconds 3
    collect_events page build/lat.exe --size 32M --mode page --min-time 0.3 --trials 5

    r=build/vtune/smoke_dram
    rm -rf "$r"
    echo "## collect memory-access: bw seq read 1 thread 256 MB"
    "$V" -collect memory-access -r "$r" -- build/bw.exe --size 256M --dur 1 --trials 3 2>&1
    echo "## report: dram summary"
    "$V" -report summary -r "$r" 2>&1 | grep -iE "bandwidth|DRAM|elapsed|GB/sec" | head -20
} > "$OUT" 2>&1
echo "done -> $OUT"
grep -iE "error|warning|cannot" "$OUT" | head -10
