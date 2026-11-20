#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the two AMD64 preview distributions without touching the host ESP.
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

mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
payload="$work/payload"
root="$payload/usr/share/infiltrator-boot"
mkdir -p "$root/boot" "$root/docs"
install -m 0644 boot/boot_x64.efi "$root/boot/boot_x64.efi"
if [[ -s gptsync/gptsync_x64.efi ]]; then
    mkdir -p "$root/boot/tools_x64"
    install -m 0644 gptsync/gptsync_x64.efi "$root/boot/tools_x64/gptsync_x64.efi"
fi
for directory in icons fonts banners themes; do
    if [[ -d "$directory" ]]; then cp -a "$directory" "$root/boot/"; fi
done
install -m 0644 boot.conf-sample "$root/boot/boot.conf-sample"
for document in COPYING.txt LICENSE.txt CREDITS.txt README.txt BUILDING.txt; do
    install -m 0644 "$document" "$root/docs/$document"
done
for script in boot-install boot-mkdefault boot-sb-healthcheck mountesp mvboot mkrlconf; do
    if [[ -f "$script" ]]; then install -m 0755 "$script" "$root/$script"; fi
done
cat > "$root/PREVIEW-NOTICE.txt" <<'NOTICE'
BOOT PREVIEW - DO NOT USE AS A VERIFIED BOOTLOADER
These packages STAGE the built EFI executable and supporting files only.
They never mount or modify the EFI System Partition, EFI/BOOT, GRUB, or NVRAM.
The BOOT source rename, UEFI operation, and Common V2 module integration
remain to be independently validated before deployment.
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

run="$out/BOOT-${version}-amd64.run"
cat > "$run" <<'RUN_HEADER'
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT self-extracting AMD64 distribution. Does not modify the ESP or NVRAM.
set -euo pipefail

extract_payload() {
    local line
    line="$(LC_ALL=C awk '/^__BOOT_ARCHIVE_BELOW__$/ { print NR + 1; exit }' "$0")"
    [[ -n "$line" ]] || { echo "Archive marker missing" >&2; return 1; }
    tail -n +"$line" "$0" | tar -xz -C "$1"
}
case "${1:---help}" in
    --extract)
        target="${2:-./BOOT-extracted}"
        mkdir -p "$target"
        extract_payload "$target"
        echo "Extracted staged files into $target"
        ;;
    --install)
        if (( EUID != 0 )); then
            echo "Use sudo for --install, or use --extract without privileges" >&2
            exit 1
        fi
        extract_payload /
        echo "BOOT staged in /usr/share/infiltrator-boot"
        echo "The EFI System Partition and NVRAM have NOT been changed."
        ;;
    --help|-h)
        echo "Usage: $0 --extract [DIRECTORY] | --install | --help"
        echo "--install stages files only; it does not change firmware boot settings."
        ;;
    *)
        echo "Unknown option: $1" >&2
        exit 2
        ;;
esac
exit 0
__BOOT_ARCHIVE_BELOW__
RUN_HEADER
tar -czf - -C "$payload" usr >> "$run"
chmod 0755 "$run"

(
    cd "$out"
    sha256sum "$(basename "$deb")" "$(basename "$run")" > SHA256SUMS
)
echo "Created $deb and $run"
