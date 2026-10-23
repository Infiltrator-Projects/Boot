#!/usr/bin/env python3
"""Regression guard: UEFI storage drivers load before independent filesystem drivers."""
from pathlib import Path

code = Path("refind/driver_support.c").read_text()
function = code[code.index("BOOLEAN LoadDrivers(VOID) {"):]
assert 'MergeStrings(&SelfDirectory, L"storage_x64"' in function
assert 'MergeStrings(&SelfDirectory, L"storage"' in function
assert 'NumFound += ScanDriverDir(SelfDirectory, FALSE);' in function
assert "while ((Directory = FindCommaDelimited(DRIVER_DIRS, i++))" in function
assert function.index('L"storage_x64"') < function.index('L"storage"')
assert function.index('L"storage"') < function.index("FindCommaDelimited(DRIVER_DIRS")
assert "ConnectFilesystemBlockDevices();" in function
assert "if (NumStorage > 0U)" in function
assert "StartEFIImage(SelfVolume, FileName" in code
assert "TRUE, FALSE, StorageProvider);" in code
launch = Path("refind/launch_efi.c").read_text()
assert "if (IsDriver && !StorageDriver && !EFI_ERROR(Status))" in launch
assert "IN BOOLEAN StorageDriver" in launch
assert "ConnectAllDriversToAllControllers();" in function
assert "RTSX" not in function and "525a" not in function
print("Boot transport-first loading contract passed")
