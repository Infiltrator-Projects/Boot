#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a generic AMD64 DEB and a source-bearing native installer RUN.
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
command -v git >/dev/null || { echo "git is required to bundle the exact source revision" >&2; exit 1; }

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

run="$out/boot-$version-native-installer.run"
cat > "$run" <<'RUN_HEADER'
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Native BOOT installer: build this machine's BOOT from bundled source and
# hand installation to the original boot-install script. No separate modes.
set -euo pipefail

if [[ $(uname -m) != x86_64 ]]; then
    echo "BOOT native installer requires an x86-64 host" >&2
    exit 2
fi
for program in make gcc ld objcopy awk tail tar mktemp; do
    command -v "$program" >/dev/null || {
        echo "BOOT native installer needs $program" >&2
        exit 1
    }
done
if [[ ! -d /usr/include/efi ]]; then
    echo "GNU-EFI development headers are missing (install gnu-efi)" >&2
    exit 1
fi

source_dir=$(mktemp -d)
cleanup() { rm -rf -- "$source_dir"; }
trap cleanup EXIT
archive_line=$(LC_ALL=C awk '/^__BOOT_SOURCE_ARCHIVE_BELOW__$/ { print NR + 1; exit }' "$0")
[[ -n $archive_line ]] || { echo "BOOT source archive marker missing" >&2; exit 1; }
tail -n +"$archive_line" "$0" | tar -xz -C "$source_dir"
cd "$source_dir"
[[ -f Makefile && -f Make.common && -f boot-install && -f boot/main.c ]] || {
    echo "The BOOT source bundle is incomplete" >&2
    exit 1
}

echo "Compiling BOOT locally for $(uname -m) with GNU-EFI..."
# UEFI requires general-purpose registers; local CPU tuning must not cause
# the compiler to emit vector instructions into firmware context.
make CFLAGS="-O2 -march=native -mtune=native -mgeneral-regs-only -fno-stack-protector -fshort-wchar -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Wall" gnuefi
[[ -s boot/boot_x64.efi ]] || { echo "Native BOOT compilation produced no EFI executable" >&2; exit 1; }

echo "Invoking BOOT's existing boot-install script..."
# Keep the repository's real installer (including its prompts and EFI/NVRAM
# handling). Do not install legacy per-filesystem EFI drivers.
bash ./boot-install --nodrivers "$@"
echo "BOOT native build and installation completed."
exit 0
__BOOT_SOURCE_ARCHIVE_BELOW__
RUN_HEADER
# The RUN contains the complete recorded source, not a precompiled EFI image.
# git archive also preserves executable modes required by the original installer.
git archive --format=tar HEAD | gzip -n -9 >> "$run"
chmod 0755 "$run"

(
    cd "$out"
    sha256sum "$(basename "$deb")" "$(basename "$run")" > SHA256SUMS
)
echo "Created $deb and $run"
