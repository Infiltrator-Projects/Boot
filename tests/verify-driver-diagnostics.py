#!/usr/bin/env python3
"""Boot firmware diagnostics: source-boundary and no-persistence regression.

Compiled EFI runtime is checked by the GNU-EFI CI build. These assertions
protect the diagnostic's contract and do not claim hardware execution.
"""
from pathlib import Path

main = Path("refind/main.c").read_text()
global_header = Path("refind/global.h").read_text()
config = Path("refind/config.c").read_text()
drivers = Path("refind/driver_support.c").read_text()
launch = Path("refind/launch_efi.c").read_text()
menu = Path("refind/menu.c").read_text()
defaults = Path("boot.conf").read_text()

assert "BOOLEAN          DriverDiagnostics;" in global_header
assert "/* DriverDiagnostics = */ FALSE" in main
assert 'L"driver_diagnostics"' in config
assert "# driver_diagnostics true" in defaults
assert "\ndriver_diagnostics true\n" not in defaults

for action in (
    "verify EFI image", "LoadImage", "StartImage", "compatibility binding",
):
    assert f'L"{action}"' in launch
for action in ("ConnectController", "compat DisconnectController",
               "compat ConnectController", "volume rescan"):
    assert f'L"{action}"' in drivers + main

# Mark entry into blocking firmware work and the result, without creating any
# new work when diagnostics are not enabled.
assert 'BootDriverDiagnosticProgress(L"StartImage", ImageTitle, FALSE' in launch
assert 'BootDriverDiagnosticProgress(L"StartImage", ImageTitle, TRUE' in launch
assert 'BootDriverDiagnosticProgress(L"ConnectController", L"BlockIo handle", FALSE' in drivers
assert 'BootDriverDiagnosticProgress(L"ConnectController", L"BlockIo handle", TRUE' in drivers
assert 'DiagNewFilesystems' in drivers and '&gMyEfiSimpleFileSystemProtocolGuid' in drivers
assert "BootDriverDiagnosticVolumes()" in main
assert "BootGetDriverDiagnosticLine(Line)" in menu
assert "GlobalConfig.DriverDiagnostics" in menu
assert "TAG_DRIVER_DIAGNOSTICS" in global_header
assert "case TAG_DRIVER_DIAGNOSTICS:" in main
assert "ShowBootDiagnostics();" in main
assert "VOID ShowBootDiagnostics(VOID)" in menu
assert "TempMenuEntry->Tag = TAG_DRIVER_DIAGNOSTICS;" in Path("refind/scan.c").read_text()
assert "if (GlobalConfig.DriverDiagnostics)" in Path("refind/scan.c").read_text()
assert "BootGetStartupTimingLine(Line)" in menu

# Neither the startup timing nor the driver diagnostics may paint into the
# graphical main-menu footer: both belong to the separately laid-out screen.
main_paint = menu[menu.index("static VOID PaintAll("):
                  menu.index("} // static VOID PaintAll()")]
assert "BootGetDriverDiagnosticLine(" not in main_paint
assert "BootGetStartupTimingLine(" not in main_paint
detail = menu[menu.index("VOID ShowBootDiagnostics(VOID)"):
              menu.index("} // VOID ShowBootDiagnostics(")] if "} // VOID ShowBootDiagnostics(" in menu else menu[menu.index("VOID ShowBootDiagnostics(VOID)"):menu.index("// Check each filename in FilenameList")]
assert "RunMenu(&Diagnostics, NULL);" in detail
assert "FreeList((VOID ***)&Diagnostics.InfoLines" in detail
assert "MyFreePool(Diagnostics.Entries)" in detail

# Keep page rows concise rather than smuggling a status bar back in.
assert "Line >= 15" in drivers
assert "Result[50] = 0" in drivers

diagnostic_functions = drivers[drivers.index("static UINTN DiagDriverAttempts"):
                               drivers.index("EFI_GUID gMyEfiLoadedImageProtocolGuid")]
for dangerous in ("SetVariable", "EFI_FILE_MODE_WRITE", "CreateFile",
                  "WriteToLog", "StartLogging"):
    assert dangerous not in diagnostic_functions, dangerous

assert "ConnectFilesystemBlockDevices();" in drivers
assert "ConnectAllDriversToAllControllers();" in drivers
assert "ConnectFilesystemDriver(ChildImageHandle);" in launch
print("Boot RAM-only EFI diagnostic source contract passed")
