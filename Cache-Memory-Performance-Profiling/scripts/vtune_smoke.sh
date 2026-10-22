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
# Takes about 1-2 minutes.
#
# Output: data/raw/vtune_smoke.txt (full log). Each check prints PASS/FAIL.
# Exit status is nonzero if any check fails. The checks are independent, so
# all three run even if one fails (no `set -e`); every step's status is
# tested explicitly instead. Each run writes VTune results into a new
# timestamped folder under build/vtune/, so nothing is ever deleted.
set -uo pipefail
cd "$(dirname "$0")/.."

V=${VTUNE:-"/c/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe"}
[ -x "$V" ] || { echo "FAIL: vtune.exe not found at $V"; exit 1; }
for t in kern lat bw; do
    [ -x build/$t.exe ] || { echo "FAIL: build/$t.exe missing (run scripts/build.sh)"; exit 1; }
done
# VTUNE_SMOKE_SELFTEST=1 (with VTUNE=<stub>) skips the admin check so the
# PASS/FAIL logic itself can be tested without elevation.
if [ "${VTUNE_SMOKE_SELFTEST:-0}" != 1 ] && ! net session >/dev/null 2>&1; then
    echo "FAIL: this Git Bash is not running as administrator; VTune cannot read counters."
    echo "      Close it, right-click Git Bash -> Run as administrator, and run again."
    exit 1
fi

# Fixed counters (cycles, instructions) + 7 programmable events.
EV="CPU_CLK_UNHALTED.THREAD,INST_RETIRED.ANY"
EV="$EV,MEM_LOAD_RETIRED.L1_HIT,MEM_LOAD_RETIRED.L2_HIT,MEM_LOAD_RETIRED.L3_HIT,MEM_LOAD_RETIRED.L3_MISS"
EV="$EV,L2_LINES_IN.ALL,LONGEST_LAT_CACHE.MISS,DTLB_LOAD_MISSES.WALK_COMPLETED"

NEV=$(echo "$EV" | tr ',' '\n' | wc -l)
STAMP=$(date +%Y%m%d-%H%M%S)
RUN=build/vtune/smoke_$STAMP
OUT=data/raw/vtune_smoke.txt
mkdir -p "$RUN"
failed=0

log() { echo "$*" | tee -a "$OUT"; }

: > "$OUT"
log "# vtune smoke $(date -u +%Y-%m-%dT%H:%M:%SZ), commit $(git rev-parse --short HEAD), results in $RUN"
"$V" --version >> "$OUT" 2>&1 || { log "FAIL: vtune --version"; failed=1; }

# check_events NAME CMD...: collect the event set, export a CSV report, and
# require the report to contain every event column and at least one data row.
check_events() {
    local name=$1 r=$RUN/$1 csv=$RUN/$1.csv
    shift
    echo "## collect events: $name: $*" >> "$OUT"
    if ! "$V" -collect-with runsa -knob event-config="$EV" -r "$r" -- "$@" >> "$OUT" 2>&1; then
        log "FAIL [$name]: collection failed (details in $OUT)"
        failed=1
        return
    fi
    if ! "$V" -report hw-events -r "$r" -group-by process -format csv -csv-delimiter comma \
            > "$csv" 2>> "$OUT"; then
        log "FAIL [$name]: report failed"
        failed=1
        return
    fi
    cat "$csv" >> "$OUT"
    local missing=""
    for e in ${EV//,/ }; do grep -q "$e" "$csv" || missing="$missing $e"; done
    local rows
    rows=$(tail -n +2 "$csv" | grep -c '[0-9]') # header excluded: event names contain digits
    if [ -n "$missing" ] || [ "$rows" -lt 1 ]; then
        log "FAIL [$name]: report incomplete (missing:${missing:- none}; data rows: $rows)"
        failed=1
    else
        log "PASS [$name]: all $NEV events present, $rows data row(s), report saved to $csv"
    fi
}

check_events saxpy build/kern.exe --kernel saxpy --n 16M --seconds 3
check_events page build/lat.exe --size 32M --mode page --min-time 0.3 --trials 5

# Memory-access analysis: must report a DRAM bandwidth figure.
r=$RUN/dram
echo "## collect memory-access: bw seq read 1 thread 256 MB" >> "$OUT"
if "$V" -collect memory-access -r "$r" -- build/bw.exe --size 256M --dur 1 --trials 3 >> "$OUT" 2>&1 \
        && "$V" -report summary -r "$r" > "$RUN/dram_summary.txt" 2>> "$OUT"; then
    cat "$RUN/dram_summary.txt" >> "$OUT"
    if grep -qiE "DRAM.*(GB/sec|bandwidth)" "$RUN/dram_summary.txt"; then
        log "PASS [dram]: memory-access analysis reports DRAM bandwidth"
    else
        log "FAIL [dram]: summary has no DRAM bandwidth figure"
        failed=1
    fi
else
    log "FAIL [dram]: memory-access collection or report failed"
    failed=1
fi

if [ "$failed" -eq 0 ]; then
    log "ALL CHECKS PASSED -> $OUT"
else
    log "SOME CHECKS FAILED -> see $OUT"
fi
exit "$failed"
