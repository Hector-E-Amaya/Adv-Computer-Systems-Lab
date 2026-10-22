#!/usr/bin/env bash
# Batch 2 hardware-counter runs (Intel VTune). MUST be run from an
# ADMINISTRATOR Git Bash:
#   Start menu -> Git Bash (right-click) -> Run as administrator, then
#   cd "/c/Users/hamay/Documents/AdvCompSys Project/Adv-Computer-Systems-Lab/Cache-Memory-Performance-Profiling"
#   scripts/run_counters.sh
# Takes about 25 minutes (each run: a few seconds of collection plus VTune's
# "Finalizing results"). Do not press keys while it runs.
#
# Runs (same 9-event set validated by vtune_smoke.sh):
#   kern_<kernel>_<n>      saxpy / mul across footprints 16 KB .. 256 MB
#                          -> misses and lines per element vs ns per element
#   lat_random_<size>      random chase per level -> hit fractions for AMAT
#   lat_page_<pages> / lat_ctrl_<pages>  page mode + compact control
#                          -> DTLB page walks per load
#
# Output: data/raw/counters/<run>.csv (hw-events per function) and
# <run>.app (the program's own CSV row, for normalisation), plus
# data/raw/counters/manifest.csv. VTune results go to a new timestamped
# folder under build/vtune/ (nothing is deleted).
#
# Sampling precision: counts are samples x sample-after value (SAV). The
# absolute error is about +-1 SAV (~1e5 events), negligible per element for
# runs of 1e8-1e10 elements/loads, so default SAVs are kept.
#
# Every run is checked: collection and report succeed, every event column is
# present, and the expected function (saxpy*/mul*/chase1) has a row. Exit
# status is nonzero if any run fails.
set -uo pipefail
cd "$(dirname "$0")/.."

V=${VTUNE:-"/c/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe"}
[ -x "$V" ] || { echo "FAIL: vtune.exe not found at $V"; exit 1; }
for t in kern lat; do
    [ -x build/$t.exe ] || { echo "FAIL: build/$t.exe missing (run scripts/build.sh)"; exit 1; }
done
# VTUNE_COUNTERS_SELFTEST=1 (with VTUNE=<stub>) skips the admin check so the
# PASS/FAIL logic can be tested without elevation.
if [ "${VTUNE_COUNTERS_SELFTEST:-0}" != 1 ] && ! net session >/dev/null 2>&1; then
    echo "FAIL: this Git Bash is not running as administrator; VTune cannot read counters."
    echo "      Close it, right-click Git Bash -> Run as administrator, and run again."
    exit 1
fi

EV="CPU_CLK_UNHALTED.THREAD,INST_RETIRED.ANY"
EV="$EV,MEM_LOAD_RETIRED.L1_HIT,MEM_LOAD_RETIRED.L2_HIT,MEM_LOAD_RETIRED.L3_HIT,MEM_LOAD_RETIRED.L3_MISS"
EV="$EV,L2_LINES_IN.ALL,LONGEST_LAT_CACHE.MISS,DTLB_LOAD_MISSES.WALK_COMPLETED"

STAMP=$(date +%Y%m%d-%H%M%S)
RUN=build/vtune/counters_$STAMP
DST=data/raw/counters
mkdir -p "$RUN" "$DST"
LOG=$DST/run_counters.log
MAN=$DST/manifest.csv
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
failed=0
total=0

log() { echo "$*" | tee -a "$LOG"; }
: > "$LOG"
echo "run,tool,function_regex,function_addr,args,app_row" > "$MAN"
log "# run_counters $(date -u +%Y-%m-%dT%H:%M:%SZ), commit $COMMIT, vtune results in $RUN"
log "# running: about 25 minutes; VTune pauses on 'Finalizing results'. Do not press keys."
trap 'log "INTERRUPTED: stopped before finishing; data/raw/counters is incomplete. Run it again."; exit 130' INT TERM HUP

# VTune may show functions as func@0x<address> (it did not use the binaries'
# symbols in the smoke test), so the timed function is identified by its
# address from the binary's own symbol table (nm), or by name if VTune has it.
func_addr() { # func_addr TOOL NAME_REGEX -> lowercase hex address without leading zeros
    # regex via ENVIRON, not -v: -v would turn "\." into "." (looser match + warning)
    RE="$2" nm "build/$1.exe" | RE="$2" awk '$2 ~ /^[tT]$/ && $3 ~ ENVIRON["RE"] {sub(/^0+/, "", $1); print tolower($1); exit}'
}

# one NAME FUNC_NAME_REGEX TOOL ARGS...
one() {
    local name=$1 fre=$2 tool=$3
    shift 3
    local r=$RUN/$name out=$RUN/$name.out raw=$RUN/$name.report.csv csv=$DST/$name.csv
    total=$((total + 1))
    local addr
    addr=$(func_addr "$tool" "$fre")
    if [ -z "$addr" ]; then
        log "FAIL [$name]: no symbol matching '$fre' in build/$tool.exe"
        failed=$((failed + 1))
        return
    fi
    if ! "$V" -collect-with runsa -knob event-config="$EV" -r "$r" -- "build/$tool.exe" "$@" > "$out" 2>&1; then
        log "FAIL [$name]: collection failed (see $out)"
        failed=$((failed + 1))
        return
    fi
    grep -E "^$tool," "$out" | tr -d '\r' > "$DST/$name.app"
    if ! "$V" -report hw-events -r "$r" -group-by function -format csv -csv-delimiter comma \
            > "$raw" 2>> "$out"; then
        log "FAIL [$name]: report failed (see $out)"
        failed=$((failed + 1))
        return
    fi
    grep -v '^vtune:' "$raw" | tr -d '\r' > "$csv" # drop VTune status lines
    local missing="" e
    for e in ${EV//,/ }; do grep -q "$e" "$csv" || missing="$missing $e"; done
    local frow
    frow=$(tail -n +2 "$csv" | grep -ciE "^\"?func@0x$addr\"?,")
    local nrow
    nrow=$(tail -n +2 "$csv" | RE="$fre" awk -F, '{f=$1; gsub(/"/, "", f)} f ~ ENVIRON["RE"]' | wc -l)
    if [ -n "$missing" ] || [ $((frow + nrow)) -lt 1 ] || [ ! -s "$DST/$name.app" ]; then
        log "FAIL [$name]: incomplete (missing events:${missing:- none}; rows for $fre @0x$addr: $((frow + nrow)); app row lines: $(wc -l < "$DST/$name.app"))"
        failed=$((failed + 1))
        return
    fi
    echo "$name,$tool,$fre,0x$addr,\"$*\",\"$(cat "$DST/$name.app")\"" >> "$MAN"
    log "PASS [$name]"
}

# 1. kernels across footprints (n elements; saxpy uses 2 arrays, mul 3)
for n in 2K 8K 32K 128K 512K 2M 8M 32M; do
    one kern_saxpy_$n '^saxpy(\.constprop\.[0-9]+)?$' kern --kernel saxpy --n $n --seconds 2
    one kern_mul_$n '^mul$' kern --kernel mul --n $n --seconds 2
done
# 2. random chase per level (AMAT hit fractions)
for s in 16K 64K 256K 1M 2M 4M 8M 16M 64M 256M; do
    one lat_random_$s '^chase1$' lat --size $s --min-time 0.3 --trials 5
done
# 3. page mode vs compact control (TLB walks)
for p in 64 1024 2048 8192 32768; do
    one lat_page_$p '^chase1$' lat --size $((p * 4096)) --mode page --min-time 0.3 --trials 5
    one lat_ctrl_$p '^chase1$' lat --size $((p * 64)) --mode random --min-time 0.3 --trials 5
done

if [ "$failed" -eq 0 ]; then
    log "ALL $total RUNS PASSED -> $DST"
else
    log "$failed OF $total RUNS FAILED -> see $LOG"
fi
exit $((failed > 0))
