#!/usr/bin/env bash
# Runs extracted native-installer functions against a disposable fake ESP.
# Fixtures include independent Windows, GRUB and EFI fallback files that must
# remain intact when BOOT reinstalls its own dedicated directory. Also
# exercises path/symlink guards and verifies the loader SHA-256 marker.

set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
awk '/^RootDir=/ {inside=1} /^GetParams "\$@"$/ {exit} inside {print}' boot-native-installer.run > "$work/functions.sh"
bash -n "$work/functions.sh"
source "$work/functions.sh"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
TargetX64=BOOTX64.EFI
Platform=x64
ShimSource=none
LocalKeys=0
InstallDrivers=all
InstallToEspOnMac=0
Upgrade=0
BootDir="$work/src/boot"
ThisDir="$work/src"
IconsDir="$work/src/icons"
ConfFile="$work/src/boot.conf-sample"
EtcKeysDir="$work/empty-keys"
mkdir -p "$InstallDir/EFI/BOOT" "$InstallDir/EFI/Microsoft/Boot" "$InstallDir/EFI/ubuntu" "$BootDir" "$IconsDir" "$EtcKeysDir" "$ThisDir/drivers_x64"
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    printf "driver-%s" "$filesystem" > "$ThisDir/drivers_x64/${filesystem}_x64.efi"
done
printf 'protected-Windows' > "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
printf 'protected-fallback' > "$InstallDir/EFI/BOOT/BOOTX64.EFI"
printf 'protected-grub' > "$InstallDir/EFI/ubuntu/grubx64.efi"
printf 'original-native-binary' > "$BootDir/boot_x64.efi"
printf 'showtools about\n' > "$ConfFile"
printf 'icon' > "$IconsDir/test.png"

set +e +u
set +o pipefail
GuardDefaultBootLocation || exit 1
CopyBootFiles
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    test -s "$InstallDir/EFI/InfiltratorOS/BOOT/drivers_x64/${filesystem}_x64.efi" || exit 1
done
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

InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
printf 'obsolete' > "$InstallDir/EFI/InfiltratorOS/BOOT/obsolete.txt"
# Seed BOOT's own directory with recognisable obsolete contents. Changes
# inside that directory and byte-for-byte preservation of the separate vendor
# files are independent expectations of a reinstall.
printf 'previous config' > "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf"
printf 'previous icons' > "$InstallDir/EFI/InfiltratorOS/BOOT/icons/old.png"
printf 'new-clean-install' > "$BootDir/boot_x64.efi"
modprobe() { :; }
FindMountedESP() { EspFilesystem=vfat; }
CheckSecureBoot() { IsSecureBoot=0; }
AddBootEntry() { :; }
GenerateBootLinuxConf() { echo "Obsolete host Linux configuration generator was called." >&2; exit 93; }
AlwaysYes=1
ExplicitDefault=0
Problems=""
InstallOnLinux > "$work/reinstall-output.txt" || exit 1
cat "$work/reinstall-output.txt"
grep -Fq "Cleared previous BOOT installation at " "$work/reinstall-output.txt" || exit 1
if grep -Fq "preserving it" "$work/reinstall-output.txt"; then
    echo "Linux reinstall incorrectly claims to preserve old BOOT configuration" >&2
    exit 1
fi
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/obsolete.txt" || exit 1
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    test -s "$InstallDir/EFI/InfiltratorOS/BOOT/drivers_x64/${filesystem}_x64.efi" || exit 1
done
test ! -e "$InstallDir/EFI/tools" || { echo "Installer wrote outside BOOT directory" >&2; exit 1; }
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/icons/old.png" || exit 1
test "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf")" = 'showtools about' || exit 1
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1

mkdir -p "$BootDir/tools_x64"
printf 'gptsync-fixture' > "$BootDir/tools_x64/gptsync_x64.efi"
( OSTYPE=darwin21; CopyTools x64 ) || exit 1
test "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/gptsync_x64.efi")" = gptsync-fixture || exit 1
test ! -e "$InstallDir/EFI/tools" || exit 1
echo "BOOT native reinstall removed old BOOT directory and preserved GRUB/Windows/fallback"

for arg in --usedefault --ownhfs --notesp --root; do
    if (GetParams "$arg" /dev/invalid >/dev/null 2>&1); then
        echo "Accepted prohibited install option: $arg" >&2
        exit 1
    fi
done
for forbidden in /EFI/BOOT /EFI/Microsoft/Boot /System/Library/CoreServices /EFI/other; do
    TargetDir="$forbidden"
    if GuardDefaultBootLocation; then
        echo "Guard accepted forbidden installation path: $forbidden" >&2
        exit 1
    fi
    if InstallOnLinux; then
        echo "Linux installer accepted forbidden path: $forbidden" >&2
        exit 1
    fi
    if InstallOnOSX; then
        echo "macOS installer accepted forbidden path: $forbidden" >&2
        exit 1
    fi
done
TargetDir=/EFI/InfiltratorOS/BOOT
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
echo "All destination overrides rejected; dedicated BOOT path enforced"
echo "Historical BOOT installer: disposable ESP safety checks passed"
