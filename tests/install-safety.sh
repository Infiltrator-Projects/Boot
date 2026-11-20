#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Nonprivileged installer safety test on a disposable fake ESP.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
awk '/^GetParams "\$@"$/ {exit} {print}' boot-install > "$work/installer-functions.sh"
# shellcheck disable=SC1090
source "$work/installer-functions.sh"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
TargetX64=BOOTX64.EFI
Platform=x64
ShimSource=none
LocalKeys=0
BootDir="$work/src/boot"
ThisDir="$work/src"
IconsDir="$work/src/icons"
ConfFile="$work/src/boot.conf-sample"
mkdir -p "$InstallDir/EFI/BOOT" "$InstallDir/EFI/Microsoft/Boot" \
         "$InstallDir/EFI/ubuntu" "$BootDir" "$IconsDir"
printf 'protected-Windows' > "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
printf 'protected-fallback' > "$InstallDir/EFI/BOOT/BOOTX64.EFI"
printf 'protected-grub' > "$InstallDir/EFI/ubuntu/grubx64.efi"
cp "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi" "$work/windows-expected"
cp "$InstallDir/EFI/BOOT/BOOTX64.EFI" "$work/fallback-expected"
cp "$InstallDir/EFI/ubuntu/grubx64.efi" "$work/grub-expected"
printf 'old-first-install' > "$BootDir/boot_x64.efi"
printf 'showtools about\n' > "$ConfFile"
printf 'dummy-icon' > "$IconsDir/test.png"
GuardDefaultBootLocation
CopyBootFiles
test -f "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI"
test -f "$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256"
test "$(sha256sum "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" | cut -d ' ' -f1)" = \
     "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256")"
RollbackBootFiles
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI"
GuardDefaultBootLocation
CopyBootFiles
FinalizeBootFiles
cp "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" "$work/previous"
printf 'new-upgrade-image' > "$BootDir/boot_x64.efi"
GuardDefaultBootLocation
CopyBootFiles
test -f "$InstallDir/EFI/InfiltratorOS/BOOT/.BOOTX64.EFI.rollback"
RollbackBootFiles
cmp "$work/previous" "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI"
test "$(sha256sum "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" | cut -d ' ' -f1)" = \
     "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256")"
# An unowned executable must never be overwritten.
rm -f "$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256"
if GuardDefaultBootLocation; then
    echo "Unowned BOOT executable was accepted" >&2
    exit 1
fi
# Both the fallback and Windows/GRUB files must be byte-identical.
cmp "$work/windows-expected" "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
cmp "$work/fallback-expected" "$InstallDir/EFI/BOOT/BOOTX64.EFI"
cmp "$work/grub-expected" "$InstallDir/EFI/ubuntu/grubx64.efi"
# A symlinked EFI path is never an installation destination.
InstallDir="$work/symlink-esp"
mkdir -p "$InstallDir"
ln -s "$work/esp/EFI" "$InstallDir/EFI"
if GuardDefaultBootLocation; then
    echo "Symlinked EFI location was accepted" >&2
    exit 1
fi
# Ensure source retains no obsolete firmware self-installer or entry deletion.
! grep -q 'InstallBoot()' boot/install.c
! grep -q -- 'efibootmgr.*-B' boot-install
echo "Disposable ESP installation, upgrade, rollback and preservation checks passed"
