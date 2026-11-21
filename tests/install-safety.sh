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
# Reproduce v0.1.98 rollback residue: resources remain after BOOTX64.EFI
# and its ownership record have been removed.
rm -f "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI"
printf 'BOOT CSV' > "$InstallDir/EFI/InfiltratorOS/BOOT/BOOT.CSV"
printf 'sample' > "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf-sample"
mkdir -p "$InstallDir/EFI/InfiltratorOS/BOOT/icons-backup"
if ! GuardDefaultBootLocation; then
    echo "Rejected BOOT resources after a previous rollback" >&2
    exit 1
fi
printf 'unknown' > "$InstallDir/EFI/InfiltratorOS/BOOT/unknown.efi"
if GuardDefaultBootLocation; then
    echo "Accepted an unknown EFI executable" >&2
    exit 1
fi
rm -f "$InstallDir/EFI/InfiltratorOS/BOOT/unknown.efi"
mv "$InstallDir/EFI/InfiltratorOS/BOOT/icons" "$work/preserved-icons"
ln -s "$work/preserved-icons" "$InstallDir/EFI/InfiltratorOS/BOOT/icons"
if GuardDefaultBootLocation; then
    echo "Accepted symlinked BOOT icons" >&2
    exit 1
fi
rm -f "$InstallDir/EFI/InfiltratorOS/BOOT/icons"
mv "$work/preserved-icons" "$InstallDir/EFI/InfiltratorOS/BOOT/icons"
GuardDefaultBootLocation || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
InstallDir="$work/link-esp"
mkdir -p "$InstallDir"
ln -s "$work/esp/EFI" "$InstallDir/EFI"
if GuardDefaultBootLocation; then echo "Accepted symlinked EFI path" >&2; exit 1; fi
echo "Historical BOOT installer: disposable ESP safety checks passed"
