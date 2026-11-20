#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a generic AMD64 DEB and a source-downloading native installer RUN.
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

# Keep the native RUN small: fetch its pinned source and original boot-install
# at execution time. No source tarball or EFI binary is embedded in the RUN.
source_sha=$(git rev-parse --verify HEAD)
[[ "$source_sha" =~ ^[a-f0-9]{40}$ ]] || { echo "Cannot pin BOOT source commit" >&2; exit 1; }
run="$out/boot-$version-native-installer.run"
cat > "$run" <<'RUN_HEADER'
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT native installer. Downloads exact GitHub source, compiles for local
# hardware, then delegates firmware installation to the ORIGINAL boot-install.
set -euo pipefail

SOURCE_SHA="__BOOT_SOURCE_COMMIT__"
SOURCE_URL='https://github.com/Infiltrator-Projects/Boot.git'

if (($# != 0)); then
    echo "BOOT native installer takes no arguments: running it builds and installs." >&2
    exit 2
fi
[[ $(uname -m) == x86_64 ]] || { echo "BOOT native installer requires x86-64" >&2; exit 2; }

required=(git make gcc ld objcopy tar mktemp)
missing=()
for program in "${required[@]}"; do
    command -v "$program" >/dev/null 2>&1 || missing+=("$program")
done
if ((${#missing[@]})) || [[ ! -f /usr/include/efi/efi.h ]]; then
    echo "Missing BOOT native build prerequisites: ${missing[*]} (and/or gnu-efi headers)" >&2
    if ! command -v apt-get >/dev/null; then
        echo "Install build-essential, binutils, git and gnu-efi before running BOOT." >&2
        exit 1
    fi
    if ((EUID == 0)); then
        apt-get -o DPkg::Lock::Timeout=300 install -y --no-install-recommends build-essential binutils git gnu-efi
    else
        command -v sudo >/dev/null || { echo "sudo is required to install build dependencies" >&2; exit 1; }
        sudo apt-get -o DPkg::Lock::Timeout=300 install -y --no-install-recommends build-essential binutils git gnu-efi
    fi
fi
for program in "${required[@]}"; do
    command -v "$program" >/dev/null || { echo "Still missing $program" >&2; exit 1; }
done
[[ -f /usr/include/efi/efi.h ]] || { echo "GNU-EFI headers are still missing" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
source_dir="$work/Boot"
git -C "$work" init -q Boot
git -C "$source_dir" remote add origin "$SOURCE_URL"
# Fetch only needed source/graphics from the pinned commit, not the whole
# repository history or a source bundle in this installer.
git -C "$source_dir" sparse-checkout init --cone
git -C "$source_dir" sparse-checkout set boot EfiLib gptsync gzip libeg mok include icons fonts banners themes keys
git -C "$source_dir" -c protocol.version=2 fetch --quiet --depth 1 --filter=blob:none origin "$SOURCE_SHA"
git -C "$source_dir" -c advice.detachedHead=false checkout --quiet --detach FETCH_HEAD
[[ $(git -C "$source_dir" rev-parse HEAD) == "$SOURCE_SHA" ]] || {
    echo "BOOT source commit does not match the published installer" >&2; exit 1;
}
cd "$source_dir"
[[ -f Makefile && -f Make.common && -f boot/main.c && -f boot-install ]] || {
    echo "Downloaded BOOT source is incomplete" >&2; exit 1;
}
bash -n ./boot-install
echo "Building BOOT natively from source commit $SOURCE_SHA..."
# Native CPU code generation, while preventing floating/vector operations in
# UEFI contexts where they are not necessarily available.
make CFLAGS="-O2 -march=native -mtune=native -mgeneral-regs-only -fno-stack-protector -fshort-wchar -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Wall" gnuefi
[[ -s boot/boot_x64.efi ]] || { echo "Native BOOT EFI build failed" >&2; exit 1; }
echo "Installing BOOT with the existing boot-install script..."
bash ./boot-install --nodrivers
echo "BOOT native compilation and installation completed."
RUN_HEADER
sed -i "s/__BOOT_SOURCE_COMMIT__/$source_sha/g" "$run"
chmod 0755 "$run"

(
    cd "$out"
    sha256sum "$(basename "$deb")" "$(basename "$run")" > SHA256SUMS
)
echo "Created $deb and $run"
