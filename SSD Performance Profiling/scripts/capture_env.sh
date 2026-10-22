#!/usr/bin/env bash
# Environment capture for the SSD project.
# Usage (from "SSD Performance Profiling/"): scripts/capture_env.sh > data/raw/env.txt
# Adapted from Project 2's capture_env.sh: adds the SSD (model, firmware,
# bus, driver), the test volume (file system, free space, cluster size), and
# the fio version. SMART/temperature need administrator rights: see
# scripts/smart_snapshot.ps1.
set -u

# tr strips CR and NUL: PowerShell prints a NUL for partitions without a drive
# letter, which made env.txt a binary file.
psh() { powershell.exe -NoProfile -Command "$1" 2>/dev/null | tr -d '\r\000' | awk 'NF'; }
ver() { if command -v "$1" >/dev/null 2>&1; then "$@" 2>&1 | head -1; else echo "$1: not found"; fi; }

echo "== Date =="; date -u +"%Y-%m-%dT%H:%M:%SZ"
echo; echo "== OS =="; psh 'Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,BuildNumber'
echo; echo "== CPU =="; psh 'Get-CimInstance Win32_Processor | Format-List Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed'
echo; echo "== Memory =="; psh 'Get-CimInstance Win32_PhysicalMemory | Format-Table Capacity,Speed -AutoSize'
echo; echo "== Frequency policy =="
psh 'powercfg /getactivescheme'
echo "Battery status (2 = on AC):"; psh 'Get-CimInstance Win32_Battery | Format-List BatteryStatus'

echo; echo "== SSD =="
psh 'Get-PhysicalDisk | Format-List FriendlyName,FirmwareVersion,BusType,MediaType,Size,HealthStatus'
psh 'Get-CimInstance Win32_DiskDrive | Format-List Model,InterfaceType,Partitions,BytesPerSector'
echo "Storage controllers / drivers:"
psh 'Get-PnpDevice -Class SCSIAdapter,HDC -PresentOnly | Format-Table FriendlyName,Status -AutoSize'

echo; echo "== Partitions and test volume =="
psh 'Get-Partition | Format-Table DiskNumber,PartitionNumber,DriveLetter,Size,Type -AutoSize'
psh 'Get-Volume -DriveLetter C | Format-List FileSystem,AllocationUnitSize,Size,SizeRemaining'

echo; echo "== Toolchain =="
FIO=$(command -v fio || ls "/c/Program Files/fio/fio.exe" 2>/dev/null)
if [ -n "$FIO" ]; then echo "fio: $FIO"; "$FIO" --version 2>&1 | head -1; else echo "fio: not found"; fi
ver python --version
ver git --version
