#!/usr/bin/env bash
# Captures hardware/software environment for reproducibility.
# Usage (from SIMD-Advantage-Profiling/): scripts/capture_env.sh > data/raw/env.txt
set -u

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_WIN=1 ;;
  *) IS_WIN=0 ;;
esac

# Run a PowerShell command, strip CR characters and blank lines.
psh() { powershell.exe -NoProfile -Command "$1" 2>/dev/null | tr -d '\r' | awk 'NF'; }
# Print the first line of "<tool> --version", or "<tool>: not found".
ver() { if command -v "$1" >/dev/null 2>&1; then "$@" 2>&1 | head -1; else echo "$1: not found"; fi; }

echo "== Date =="; date -u +"%Y-%m-%dT%H:%M:%SZ"

echo; echo "== OS / Kernel =="; uname -a
[ -f /etc/os-release ] && grep -E '^(PRETTY_NAME)=' /etc/os-release
[ "$IS_WIN" = 1 ] && psh 'Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,BuildNumber'

echo; echo "== CPU =="
if [ "$IS_WIN" = 1 ]; then
  psh 'Get-CimInstance Win32_Processor | Format-List Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,L2CacheSize,L3CacheSize'
else
  lscpu 2>/dev/null || sysctl -a 2>/dev/null | grep -E 'machdep.cpu|hw\.(model|ncpu|cpufrequency|l1|l2|l3)'
fi

echo; echo "== SIMD flags (x86) =="
grep -o -w -E 'sse4_2|avx|avx2|fma|avx512[a-z0-9_]*' /proc/cpuinfo 2>/dev/null | sort -u | tr '\n' ' '; echo

echo; echo "== Cache sizes (KB on Windows; L1 listed as totals across cores) =="
if [ "$IS_WIN" = 1 ]; then
  psh 'Get-CimInstance Win32_CacheMemory | Format-Table Purpose,InstalledSize,Associativity -AutoSize'
else
  lscpu -C 2>/dev/null || lscpu | grep -i cache
fi

echo; echo "== Frequency policy =="
if [ "$IS_WIN" = 1 ]; then
  psh 'powercfg /getactivescheme'
  echo "Battery status (2 = on AC; empty = no battery):"
  psh 'Get-CimInstance Win32_Battery | Format-List BatteryStatus'
else
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "n/a"
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || true
  cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null | sed 's/^/intel_pstate no_turbo=/' || true
fi

echo; echo "== SMT =="
if [ "$IS_WIN" = 1 ]; then
  cores=$(psh '(Get-CimInstance Win32_Processor).NumberOfCores' | head -1)
  logical=$(psh '(Get-CimInstance Win32_Processor).NumberOfLogicalProcessors' | head -1)
  echo "physical cores=${cores:-?} logical processors=${logical:-?}"
  if [ -n "${cores:-}" ] && [ -n "${logical:-}" ] && [ "$logical" -gt "$cores" ]; then
    echo "SMT: enabled"
  else
    echo "SMT: disabled or unknown"
  fi
else
  cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo "n/a"
  cat /sys/devices/system/cpu/smt/active 2>/dev/null || true
fi

echo; echo "== Memory =="
if [ "$IS_WIN" = 1 ]; then
  psh 'Get-CimInstance Win32_PhysicalMemory | Format-Table Capacity,Speed,ConfiguredClockSpeed -AutoSize'
else
  free -h 2>/dev/null || sysctl hw.memsize 2>/dev/null
fi

echo; echo "== Hugepages / THP =="
cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo "n/a (Windows: large pages need a privilege; skipped for this project)"

echo; echo "== Toolchain =="
ver gcc --version
ver clang --version
ver objdump --version
ver python --version
ver perf --version