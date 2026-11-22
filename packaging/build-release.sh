#!/usr/bin/env bash
# Creates a generic AMD64 .deb that installs the prebuilt EFI binary from
# its postinst, plus a version-pinned native .run that builds then installs. The released source SHA must
# match this exact checkout; checksums protect the resulting artifact bytes.

# Packaging ties published installer artifacts to a specific source revision.
# The .deb contains generic prebuilt EFI images; the native .run builds for
# the host CPU and installs, without an exposed build-only mode.
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

bash themes/render.sh
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
fontcount=$(find fonts -maxdepth 1 -type f -name '*.ttf' | wc -l)
[[ "$fontcount" -eq 23 ]] || { echo "BOOT requires exactly 23 MB/Daimler TrueType fonts (found $fontcount)" >&2; exit 1; }
[[ $(wc -l < fonts/SHA256SUMS) -eq 23 ]] || { echo "Invalid 23-font checksum manifest" >&2; exit 1; }
( cd fonts && sha256sum -c SHA256SUMS >/dev/null ) || { echo "MB/Daimler font checksum verification failed" >&2; exit 1; }
for directory in icons fonts banners themes keys; do
    if [[ -d "$directory" ]]; then cp -a "$directory" "$root/boot/"; fi
done
install -m 0644 boot.conf-sample "$root/boot/boot.conf-sample"

# Ship the maintained technical Markdown, not the deleted legacy .txt files.
# Repository-relative links to C sources remain online-source references;
# their linked source files are not part of the staged binary package.
install -m 0644 README.md "$root/README.md"
for document in docs/*.md; do
    [[ -f "$document" ]] || continue
    install -m 0644 "$document" "$root/docs/"
done
for script in boot-sb-healthcheck mountesp; do
    if [[ -f "$script" ]]; then install -m 0755 "$script" "$root/$script"; fi
done
package="$work/debian-package"
mkdir -p "$package/DEBIAN"
cp -a "$payload/usr" "$package/usr"
# Post-installation runs at dpkg configure time, on the host machine. No build
# tools or source checkout are needed for the generic binary installation.
install -d -m 0755 "$package/usr/lib/infiltrator-boot"
install -m 0755 packaging/install-generic.sh "$package/usr/lib/infiltrator-boot/install-generic.sh"
cat > "$package/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
case "$1" in
    configure)
        exec /usr/lib/infiltrator-boot/install-generic.sh
        ;;
    *)
        exit 0
        ;;
esac
POSTINST
chmod 0755 "$package/DEBIAN/postinst"
cat > "$package/DEBIAN/control" <<EOF
Package: infiltrator-boot
Version: $version
Section: admin
Priority: optional
Architecture: amd64
Depends: bash, efibootmgr, util-linux, coreutils, libc-bin
Maintainer: Infiltrator Projects
Description: BOOT generic AMD64 UEFI boot manager installer
 This package supplies the generic AMD64 BOOT firmware image and drivers.
 During package configuration, the installer copies them to
 /EFI/InfiltratorOS/BOOT on the mounted ESP and registers a UEFI boot option.
EOF
deb="$out/infiltrator-boot_${version}_amd64.deb"
dpkg-deb --root-owner-group --build "$package" "$deb"
# Substitute the checkout commit into the native installer before hashing it.
# That SHA names the sources the installer will fetch. SHA256SUMS below
# describes the bytes of the two finished downloads.
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
