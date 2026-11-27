#!/usr/bin/env bash
# BOOT / Native installation tests
#
# Exercises selected native-installer functions against a disposable ESP,
# preserving independent vendor boot paths.
#
# The fixture has no authority to modify the host's real firmware variables
# or EFI System Partition.

set -euo pipefail
# The default configuration must offer firmware-registered operating systems
# even when Linux filesystem driver discovery is incomplete. BOOT itself must
# not appear as a recursive selection.
grep -Eq '^scanfor[[:space:]]+f([,[:space:]]|$)' boot.conf-sample
grep -Fqx 'dont_scan_firmware "BOOT Boot Manager"' boot.conf-sample
# A scanfor directive must not consume a scan slot for its own keyword.
# In the host parser, the first configured mode is TokenList[1].
grep -Fq 'GlobalConfig.ScanFor[i] = TokenList[i + 1][0];' boot/config.c
grep -Fq 'if (i + 1 < TokenCount)' boot/config.c

# Explicitly excluded partitions must never be handed to firmware
# ConnectController or re-probed for raw bootcode/SimpleFS. This is a source
# contract check; the real EFI interaction is exercised by native boot tests.
grep -Fq 'BootVolumeExcluded(Volumes[VolumeIndex])' boot/driver_support.c
grep -Fq 'BootVolumeExcluded(Volume)' boot/lib.c
grep -Fq 'BootVolumeExcluded(Volume)' boot/scan.c
grep -Fq 'Skipping excluded volume' boot/lib.c
grep -Fq 'dont_scan_volumes TEST_PARTITION_LABEL' boot.conf-sample

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
FontsDir="$work/src/fonts"
ThemesDir="$work/src/themes"
ConfFile="$work/src/boot.conf-sample"
EtcKeysDir="$work/empty-keys"
mkdir -p "$InstallDir/EFI/BOOT" "$InstallDir/EFI/Microsoft/Boot" "$InstallDir/EFI/ubuntu" "$BootDir" "$IconsDir" "$FontsDir" "$ThemesDir" "$EtcKeysDir" "$ThisDir/drivers_x64"
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    printf "driver-%s" "$filesystem" > "$ThisDir/drivers_x64/${filesystem}_x64.efi"
done
printf 'protected-Windows' > "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
printf 'protected-fallback' > "$InstallDir/EFI/BOOT/BOOTX64.EFI"
printf 'protected-grub' > "$InstallDir/EFI/ubuntu/grubx64.efi"
printf 'original-native-binary' > "$BootDir/boot_x64.efi"
printf 'showtools about\n' > "$ConfFile"
printf 'icon' > "$IconsDir/test.png"
cp -a fonts/. "$FontsDir/"
test -s themes/boot-luxury.jpg || exit 1
cp themes/boot-luxury.jpg "$ThemesDir/"

# Exercise the fixed path without requiring a marker or recognising previous
# BOOT contents. A reinstall deliberately clears the complete BOOT directory.
set +e +u
set +o pipefail
CheckBootDestination || exit 1
CopyBootFiles
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    test -s "$InstallDir/EFI/InfiltratorOS/BOOT/drivers_x64/${filesystem}_x64.efi" || exit 1
done
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/.infiltrator-boot-loader.sha256" || exit 1
while read -r digest face; do
    cmp "$FontsDir/$face" "$InstallDir/EFI/InfiltratorOS/BOOT/fonts/$face" || exit 1
done < "$FontsDir/SHA256SUMS"
cmp "$ThemesDir/boot-luxury.jpg" "$InstallDir/EFI/InfiltratorOS/BOOT/themes/boot-luxury.jpg" || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-Windows || exit 1
test "$(cat "$InstallDir/EFI/BOOT/BOOTX64.EFI")" = protected-fallback || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/grubx64.efi")" = protected-grub || exit 1
InstallDir="$work/link-esp"
mkdir -p "$InstallDir"
ln -s "$work/esp/EFI" "$InstallDir/EFI"
if CheckBootDestination; then echo "Accepted symlinked EFI path" >&2; exit 1; fi

InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
printf 'obsolete' > "$InstallDir/EFI/InfiltratorOS/BOOT/obsolete.txt"
printf 'old module' > "$InstallDir/EFI/InfiltratorOS/BOOT/drivers_x64/ext4.so"
printf 'old module' > "$InstallDir/EFI/InfiltratorOS/BOOT/drivers_x64/ntfs.SO"
printf 'source module must not be installed' > "$ThisDir/drivers_x64/ntfs.so"
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
test -z "$(find "$InstallDir/EFI/InfiltratorOS/BOOT" -iname '*.so' -print)" || exit 1
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
    if CheckBootDestination; then
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

# Secure Boot must never change ESP files or firmware boot order before the
# locally signed loader's MOK is enrolled. Model both decisions in a disposable
# directory, with command stubs instead of real firmware or key mutations.
InstallDir="$work/secureboot-esp"
mkdir -p "$InstallDir/EFI/Microsoft/Boot" "$InstallDir/EFI/ubuntu"
printf 'protected-ubuntu' > "$InstallDir/EFI/ubuntu/shimx64.efi"
printf 'protected-windows' > "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi"
ShimSource="$work/shimx64.efi"
MokManagerSource="$work/mmx64.efi"
printf 'trusted-shim-fixture' > "$ShimSource"
printf 'mokmanager-fixture' > "$MokManagerSource"
EtcKeysDir="$work/empty-keys"
LocalKeysBase=boot_local
printf 'local-key-fixture' > "$EtcKeysDir/boot_local.cer"
printf 'local-cert-fixture' > "$EtcKeysDir/boot_local.crt"
DerKey="$EtcKeysDir/boot_local.cer"
CertKey="$EtcKeysDir/boot_local.crt"
LocalKeys=1
KeepName=1
TargetX64=BOOTX64.EFI
TargetShim=shimx64.efi
ReSignBinaries() { :; }
# This fixture must never inspect the test runner's real certificates.
DiscoverEnrolledSigningIdentity() { :; }
CheckSecureBoot() { IsSecureBoot=1; }
AddBootEntry() { echo "Firmware entry fixture" >&2; return 0; }
mokutil() { echo "$DerKey is not enrolled"; return 0; }
sbverify() { return 0; }
if InstallOnLinux > "$work/secureboot-pending.txt" 2>&1; then
    echo "BOOT accepted Secure Boot with unenrolled local MOK" >&2
    exit 1
fi
grep -Fq 'could not be verified as enrolled' "$work/secureboot-pending.txt" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/boot.conf" || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/shimx64.efi")" = protected-ubuntu || exit 1
mokutil() { echo "$DerKey is already enrolled"; return 1; }
if ! InstallOnLinux > "$work/secureboot-enrolled.txt" 2>&1; then
    cat "$work/secureboot-enrolled.txt" >&2
    exit 1
fi
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI" || exit 1
test -s "$InstallDir/EFI/InfiltratorOS/BOOT/shimx64.efi" || exit 1
test ! -e "$InstallDir/EFI/InfiltratorOS/BOOT/grubx64.efi" || exit 1
test "$(cat "$InstallDir/EFI/ubuntu/shimx64.efi")" = protected-ubuntu || exit 1
test "$(cat "$InstallDir/EFI/Microsoft/Boot/bootmgfw.efi")" = protected-windows || exit 1
echo "Secure Boot MOK gate and one-BOOT-image Shim fixture passed"


# A successful Secure Boot copy removes its temporary signed source tree.
# Firmware may be promoted only after verifying the installed bytes against
# a digest captured before that temporary tree was removed.
BootDir="$work/staged-signed"
mkdir -p "$BootDir/drivers_x64"
printf 'signed-native-efi-image' > "$BootDir/boot_x64.efi"
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    printf 'signed-%s' "$filesystem" > "$BootDir/drivers_x64/\${filesystem}_x64.efi"
done
DeleteBootDir=1
Problems=""
if ! InstallOnLinux > "$work/secureboot-staged.txt" 2>&1; then
    cat "$work/secureboot-staged.txt" >&2
    exit 1
fi
test ! -d "$BootDir" || { echo "Temporary signed source was not removed" >&2; exit 1; }
test "$(cat "$InstallDir/EFI/InfiltratorOS/BOOT/BOOTX64.EFI")" = signed-native-efi-image || exit 1
echo "Signed-image digest survived temporary source cleanup and permitted promotion"

echo "Historical BOOT installer: disposable ESP safety checks passed"
