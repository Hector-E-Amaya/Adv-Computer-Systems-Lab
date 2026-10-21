#!/usr/bin/env bash
# Captures hardware/software environment for reproducibility.
# Usage (from Cache-Memory-Performance-Profiling/): scripts/capture_env.sh > data/raw/env.txt
# Adapted from SIMD-Advantage-Profiling/scripts/capture_env.sh, adding the memory
# details this project depends on: DIMM slots/channels, page sizes, large-page
# privilege, virtualization-based security (can block PMU counters), VTune.
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
  lscpu 2>/dev/null
fi

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
fi

echo; echo "== Memory modules (slot / bank tell single vs dual channel) =="
if [ "$IS_WIN" = 1 ]; then
  psh 'Get-CimInstance Win32_PhysicalMemory | Format-Table BankLabel,DeviceLocator,Capacity,Speed,ConfiguredClockSpeed,Manufacturer,PartNumber -AutoSize'
  psh 'Get-CimInstance Win32_PhysicalMemoryArray | Format-List MemoryDevices,MaxCapacityEx'
else
  free -h 2>/dev/null
fi

echo; echo "== Pages =="
if [ "$IS_WIN" = 1 ]; then
  python -I -c "import ctypes
k = ctypes.windll.kernel32
class SI(ctypes.Structure):
    _fields_ = [('a', ctypes.c_uint16), ('b', ctypes.c_uint16), ('PageSize', ctypes.c_uint32),
                ('lo', ctypes.c_void_p), ('hi', ctypes.c_void_p), ('mask', ctypes.c_void_p),
                ('ncpu', ctypes.c_uint32), ('type', ctypes.c_uint32), ('gran', ctypes.c_uint32),
                ('lvl', ctypes.c_uint16), ('rev', ctypes.c_uint16)]
si = SI(); k.GetSystemInfo(ctypes.byref(si))
k.GetLargePageMinimum.restype = ctypes.c_size_t
print('page size bytes:', si.PageSize)
print('large page minimum bytes:', k.GetLargePageMinimum())" 2>&1
  if whoami //priv 2>/dev/null | grep -q SeLockMemoryPrivilege; then
    echo "SeLockMemoryPrivilege (large pages): GRANTED to this user"
  else
    echo "SeLockMemoryPrivilege (large pages): not granted to this user"
  fi
else
  cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null
  grep -i huge /proc/meminfo 2>/dev/null
fi

echo; echo "== Virtualization-based security (can block hardware counters) =="
if [ "$IS_WIN" = 1 ]; then
  psh 'Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard | Format-List VirtualizationBasedSecurityStatus,SecurityServicesRunning'
  echo "(VBS status: 0 = off, 1 = enabled not running, 2 = running; services 2 = memory integrity / HVCI)"
  psh '(Get-CimInstance Win32_ComputerSystem).HypervisorPresent' | sed 's/^/HypervisorPresent: /'
fi

echo; echo "== Toolchain =="
ver gcc --version
ver python --version
ver git --version
vt=""
for c in vtune "/c/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe" \
         "/c/Program Files/Intel/oneAPI/vtune/latest/bin64/vtune.exe"; do
  if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then vt=$c; break; fi
done
if [ -n "$vt" ]; then "$vt" --version 2>&1 | head -1; else echo "vtune: not found"; fi
