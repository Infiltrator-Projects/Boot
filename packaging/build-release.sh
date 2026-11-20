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
# COPYING.txt is the package's licence. Other documentation is optional:
# deleting upstream prose must never break the native installer release.
install -m 0644 COPYING.txt "$root/docs/COPYING.txt"
for document in README.txt BUILDING.txt; do
    if [[ -f "$document" ]]; then
        install -m 0644 "$document" "$root/docs/$document"
    fi
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

# Embed the original boot-install implementation directly into the native RUN.
# Only BOOT's build sources are downloaded at execution time. The release
# contains no embedded EFI executable or complete source repository.
source_sha=$(git rev-parse --verify HEAD)
[[ "$source_sha" =~ ^[a-f0-9]{40}$ ]] || { echo "Cannot pin BOOT source commit" >&2; exit 1; }
test -s boot-install || { echo "Missing original BOOT installer source" >&2; exit 1; }
bash -n boot-install
# Embed the exact installer version committed alongside the pinned build source.
git diff --quiet HEAD -- boot-install || {
    echo "BOOT installer source has uncommitted changes; refusing mixed release" >&2
    exit 1
}
run="$out/boot-$version-native-installer.run"
cat > "$run" <<'RUN_HEADER'
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT native installer. Downloads pinned build sources and compiles natively.
# The entire original boot-install implementation is included below in this RUN.
set -euo pipefail

SOURCE_SHA="__BOOT_SOURCE_COMMIT__"
SOURCE_URL='https://github.com/Infiltrator-Projects/Boot.git'

if (($# != 0)); then
    echo "BOOT native installer takes no arguments: running it builds and installs." >&2
    exit 2
fi
[[ $(uname -m) == x86_64 ]] || { echo "BOOT native installer requires x86-64" >&2; exit 2; }

# The combined installer must be elevated before entering its inline
# boot-install code. No second installer script is executed through sudo.
if ((EUID != 0)); then
    command -v sudo >/dev/null || { echo "sudo is required for EFI installation" >&2; exit 1; }
    exec sudo bash "$0"
fi

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
[[ -f Makefile && -f Make.common && -f boot/main.c ]] || {
    echo "Downloaded BOOT build source is incomplete" >&2; exit 1;
}
echo "Building BOOT natively from source commit $SOURCE_SHA..."
# Native CPU code generation, while preventing floating/vector operations in
# UEFI contexts where they are not necessarily available.
make CFLAGS="-O2 -march=native -mtune=native -mgeneral-regs-only -fno-stack-protector -fshort-wchar -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Wall" gnuefi
[[ -s boot/boot_x64.efi ]] || { echo "Native BOOT EFI build failed" >&2; exit 1; }
echo "Running the installation logic embedded in this native RUN..."
RUN_HEADER
# Define the original installer as a function so its ordinary exit codes and
# failure handling remain intact, without needing any second installer file.
printf '\nembedded_boot_install() {\n# EMBEDDED_BOOT_INSTALL_BEGIN\n' >> "$run"
cat boot-install >> "$run"
printf '# EMBEDDED_BOOT_INSTALL_END\n}\n' >> "$run"
cat >> "$run" <<'RUN_FOOTER'
# The original script resolves its resources relative to its own location.
# The downloaded source contains those resources; the wrapper itself does not.
export BOOT_INSTALL_SOURCE_DIR="$source_dir"
# A standalone 'bash boot-install' would not inherit strict wrapper options.
# Preserve the original installer's own error and exit-status semantics.
set +e +u
set +o pipefail
embedded_boot_install --nodrivers
RUN_FOOTER
sed -i "s/__BOOT_SOURCE_COMMIT__/$source_sha/g" "$run"
chmod 0755 "$run"

(
    cd "$out"
    sha256sum "$(basename "$deb")" "$(basename "$run")" > SHA256SUMS
)
echo "Created $deb and $run"
