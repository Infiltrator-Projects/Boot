#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Disposable fake ESP checks for the full-length BOOT-adapted installer.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
awk '/^GetParams "\$@"$/ {exit} {print}' boot-install > "$work/functions.sh"
bash -n "$work/functions.sh"
source "$work/functions.sh"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
TargetX64=BOOTX64.EFI
Platform=x64
ShimSource=none
LocalKeys=0
InstallDrivers=none
InstallToEspOnMac=0
Upgrade=0
BootDir="$work/src/boot"
ThisDir="$work/src"
IconsDir="$work/src/icons"
ConfFile="$work/src/boot.conf-sample"
EtcKeysDir="$work/empty-keys"
mkdir -p "$InstallDir/EFI/BOOT" "$InstallDir/EFI/Microsoft/Boot" "$InstallDir/EFI/ubuntu" "$BootDir" "$IconsDir" "$EtcKeysDir"
printf 'protected-Windows' > "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
printf 'protected-fallback' > "$InstallDir/EFI/BOOT/BOOTX64.EFI"
printf 'protected-grub' > "$InstallDir/EFI/ubuntu/grubx64.efi"
printf 'original-native-binary' > "$BootDir/boot_x64.efi"
printf 'showtools about\n' > "$ConfFile"
printf 'icon' > "$IconsDir/test.png"
# The native RUN invokes the historical script without strict shell options.
set +e +u
set +o pipefail
GuardDefaultBootLocation || exit 1
CopyBootFiles
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
marker="$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256"
test "$(sha256sum "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" | cut -d ' ' -f1)" = "$(cat "$marker")" || exit 1
GuardDefaultBootLocation || exit 1
rm -f "$marker"
if GuardDefaultBootLocation; then echo "Accepted unowned loader" >&2; exit 1; fi
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
InstallDir="$work/link-esp"
mkdir -p "$InstallDir"
ln -s "$work/esp/EFI" "$InstallDir/EFI"
if GuardDefaultBootLocation; then echo "Accepted symlinked EFI path" >&2; exit 1; fi
# End-to-end installation path: existing BOOT resources are removed before
# the original CopyBootFiles runs, while other EFI vendors are untouched.
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
printf 'obsolete' > "$InstallDir/EFI/InfiltratorOS/BOOT/obsolete.txt"
printf 'previous config' > "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf"
printf 'previous icons' > "$InstallDir/EFI/InfiltratorOS/BOOT/icons/old.png"
printf 'new-clean-install' > "$BootDir/boot_x64.efi"
modprobe() { :; }
FindMountedESP() { EspFilesystem=vfat; }
CheckSecureBoot() { IsSecureBoot=0; }
AddBootEntry() { :; }
GenerateBootLinuxConf() { :; }
AlwaysYes=1
ExplicitDefault=0
Problems=""
InstallOnLinux || exit 1
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/obsolete.txt" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/icons/old.png" || exit 1
test "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf")" = 'showtools about' || exit 1
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1
echo "BOOT native reinstall removed old BOOT directory and preserved GRUB/Windows/fallback"
echo "Historical BOOT installer: disposable ESP safety checks passed"
