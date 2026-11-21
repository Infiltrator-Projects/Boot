#!/usr/bin/env bash
# Creates two fundamentally different deliverables: a generic AMD64 .deb
# that stages files without writing firmware, and a version-pinned .run that
# performs native installation when executed. The released source SHA must
# match this exact checkout; checksums protect the resulting artifact bytes.


set -euo pipefail

version="${1:?Usage: packaging/build-release.sh VERSION [OUTPUT_DIRECTORY]}"
out="${2:-dist}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version must be numeric MAJOR.MINOR.PATCH" >&2
    exit 2
fi
if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "This release profile builds AMD64 packages only" >&2
    exit 2
fi
test -s boot/boot_x64.efi || { echo "Missing compiled boot/boot_x64.efi" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required" >&2; exit 1; }
command -v git >/dev/null || { echo "git is required to pin the exact source revision" >&2; exit 1; }

mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
payload="$work/payload"
root="$payload/usr/share/infiltrator-boot"
mkdir -p "$root/boot" "$root/docs"
install -m 0644 boot/boot_x64.efi "$root/boot/boot_x64.efi"
mkdir -p "$root/boot/drivers_x64"
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    driver="drivers_x64/${filesystem}_x64.efi"
    test -s "$driver" || { echo "Missing EFI filesystem driver $driver" >&2; exit 1; }
    install -m 0644 "$driver" "$root/boot/drivers_x64/"
done
for directory in icons fonts banners themes; do
    if [[ -d "$directory" ]]; then cp -a "$directory" "$root/boot/"; fi
done
install -m 0644 boot.conf-sample "$root/boot/boot.conf-sample"


for document in README.txt BUILDING.txt; do
    if [[ -f "$document" ]]; then
        install -m 0644 "$document" "$root/docs/$document"
    fi
done
for script in boot-sb-healthcheck mountesp; do
    if [[ -f "$script" ]]; then install -m 0755 "$script" "$root/$script"; fi
done
cat > "$root/PREVIEW-NOTICE.txt" <<'NOTICE'
BOOT PREVIEW - DO NOT USE AS A VERIFIED BOOTLOADER
These packages STAGE the built EFI executable and supporting files only.
They never mount or modify the EFI System Partition, EFI/BOOT, GRUB, or NVRAM.
The six standard EFI filesystem drivers are staged with BOOT.
Direct kernel boot still requires verification on real hardware.
NOTICE

package="$work/debian-package"
mkdir -p "$package/DEBIAN"
cp -a "$payload/usr" "$package/usr"
cat > "$package/DEBIAN/control" <<EOF
Package: infiltrator-boot
Version: $version
Section: admin
Priority: optional
Architecture: amd64
Maintainer: Infiltrator Projects
Description: BOOT UEFI boot manager preview (staged assets only)
 This package stages the BOOT firmware image and resources in
 /usr/share/infiltrator-boot. It does not install to the EFI System
 Partition or modify EFI firmware boot entries.
EOF
deb="$out/infiltrator-boot_${version}_amd64.deb"
dpkg-deb --root-owner-group --build "$package" "$deb"



source_sha=$(git rev-parse --verify HEAD)
[[ "$source_sha" =~ ^[a-f0-9]{40}$ ]] || { echo "Cannot pin BOOT source commit" >&2; exit 1; }
test -s boot-native-installer.run || { echo "Missing native RUN source" >&2; exit 1; }
bash -n boot-native-installer.run
git diff --quiet HEAD -- boot-native-installer.run || {
    echo "Native RUN source has uncommitted changes; refusing mixed release" >&2
    exit 1
}
run="$out/boot-$version-native-installer.run"
cp boot-native-installer.run "$run"
sed -i "s/__BOOT_SOURCE_COMMIT__/$source_sha/g; s/__BOOT_RELEASE_VERSION__/$version/g" "$run"
chmod 0755 "$run"

(
    cd "$out"
    sha256sum "$(basename "$deb")" "$(basename "$run")" > SHA256SUMS
)
echo "Created $deb and $run"
