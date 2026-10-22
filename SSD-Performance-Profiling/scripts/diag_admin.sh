#!/usr/bin/env bash
# READ-ONLY storage diagnostics that need administrator rights. Changes
# nothing: it only queries settings and health counters.
# Run from an ADMINISTRATOR Git Bash:
#   cd "/c/Users/hamay/Documents/AdvCompSys Project/Adv-Computer-Systems-Lab/SSD-Performance-Profiling"
#   scripts/diag_admin.sh
# Output: data/raw/diag_admin.txt
set -u
cd "$(dirname "$0")/.."
net session >/dev/null 2>&1 || { echo "FAIL: not running as administrator"; exit 1; }
OUT=data/raw/diag_admin.txt
psh() { powershell.exe -NoProfile -Command "$1" 2>&1 | tr -d '\r\000' | awk 'NF'; }
{
    echo "# diag_admin $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "== Device write cache (IsDeviceCacheEnabled) =="
    psh 'Get-PhysicalDisk | Get-StorageAdvancedProperty | Format-List'
    echo "== BitLocker (encryption method, hardware vs software, status) =="
    manage-bde -status C: 2>&1 | tr -d '\r'
    echo "== SMART / reliability counters (temperature, wear, writes) =="
    psh 'Get-PhysicalDisk | Get-StorageReliabilityCounter | Format-List *'
} > "$OUT"
echo "done -> $OUT"
grep -iE "IsDeviceCacheEnabled|Encryption Method|Conversion Status|Percentage Encrypted|Temperature|Wear|WriteErrors|PowerOnHours" "$OUT"
