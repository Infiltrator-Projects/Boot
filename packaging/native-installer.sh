#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the independently owned BOOT EFI application on the target machine.
set -euo pipefail
export LC_ALL=C
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

build_only=0
output_dir=
while (($#)); do
    case "$1" in
        --system-package-mode) ;;
        --build-only) build_only=1 ;;
        --output-dir) shift; output_dir=${1:?Missing output directory} ;;
        *) printf 'Unknown BOOT installer option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done
[[ $(uname -m) == x86_64 ]] || { echo 'BOOT requires x86-64.' >&2; exit 1; }
if (( build_only )); then
    [[ -n $output_dir ]] || { echo '--build-only requires --output-dir.' >&2; exit 2; }
    mkdir -p -- "$output_dir"
    output_dir=$(cd -- "$output_dir" && pwd)
elif [[ $(id -u) != 0 ]]; then
    echo 'Run the BOOT installer as root.' >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/infiltrator-boot-native.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
payload_line=$(awk '/^__BOOT_NATIVE_PAYLOAD__$/ {print NR + 1; exit}' "$0")
[[ $payload_line =~ ^[0-9]+$ ]] || { echo 'Missing BOOT source payload.' >&2; exit 1; }
tail -n +"$payload_line" "$0" > "$work/payload.tar.gz"
printf '%s  %s\n' '@PAYLOAD_SHA256@' "$work/payload.tar.gz" | sha256sum -c -
mkdir "$work/input"
tar -xzf "$work/payload.tar.gz" -C "$work/input" --no-same-owner --no-same-permissions
source_root="$work/input/source"
resources="$work/input/resources"
version=$(tr -d '\r\n' < "$source_root/VERSION")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1

if ! command -v gcc >/dev/null || ! command -v make >/dev/null ||
   ! command -v objcopy >/dev/null ||
   { [[ -z ${GNU_EFI_PREFIX:-} ]] && ! dpkg-query -W -f='${Status}' gnu-efi 2>/dev/null | grep -qx 'install ok installed'; }; then
    if [[ $(id -u) != 0 ]]; then
        echo 'Install gcc, make, binutils and gnu-efi to build BOOT.' >&2
        exit 1
    fi
    export DEBIAN_FRONTEND=noninteractive
    apt-get -o DPkg::Lock::Timeout=-1 update
    apt-get -o DPkg::Lock::Timeout=-1 install -y --no-install-recommends gcc make binutils gnu-efi
fi

efi_prefix=${GNU_EFI_PREFIX:-/usr}
if [[ -n ${GNU_EFI_PREFIX:-} ]]; then
    files=$(find "$efi_prefix/lib" -type f \( -name crt0-efi-x86_64.o -o -name libefi.a -o -name libgnuefi.a \))
else
    files=$(dpkg -L gnu-efi)
fi
crt=$(grep '/crt0-efi-x86_64\.o$' <<< "$files" | head -n1)
efi=$(grep '/libefi\.a$' <<< "$files" | head -n1)
gnu=$(grep '/libgnuefi\.a$' <<< "$files" | head -n1)
test -f "$crt" && test -f "$efi" && test -f "$gnu"
options=(ARCH=x86_64 "EFIINC=$efi_prefix/include/efi" "EFICRT0=$(dirname "$crt")"
         "EFILIB=$(dirname "$efi")" "GNUEFILIB=$(dirname "$gnu")"
         'OPTIMFLAGS=-O3 -march=native -mgeneral-regs-only -fno-tree-vectorize -fno-strict-aliasing -fno-tree-loop-distribute-patterns')
jobs=$(nproc)
for component in libeg mok gzip EfiLib refind; do
    make -C "$source_root/$component" -j"$jobs" MAKEWITH=GNUEFI "${options[@]}"
done
executable="$source_root/refind/refind_x64.efi"
test -s "$executable"
objdump -f "$executable" | grep -q 'file format pei-x86-64'
objdump -p "$executable" | grep -Eq 'Subsystem[[:space:]]+0*0a[[:space:]]+\(EFI application\)'

package="$work/package"
mkdir -p "$package/DEBIAN" "$package/usr/lib/infiltrator-boot" "$package/usr/sbin" "$package/usr/share/doc/infiltrator-boot"
cp -a "$resources/." "$package/usr/lib/infiltrator-boot/"
install -m 0644 "$executable" "$package/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI"
install -m 0644 "$source_root/packaging/infiltrator-boot-deploy-core" "$package/usr/lib/infiltrator-boot/deploy-core"
install -m 0755 "$source_root/packaging/infiltrator-boot-deploy" "$package/usr/sbin/infiltrator-boot-deploy"
printf '%s\n' "$version" > "$package/usr/lib/infiltrator-boot/VERSION"
printf '%s\n' 'Profile: aggressive' 'Compiler flags: -O3 -march=native -mgeneral-regs-only' \
    > "$package/usr/share/doc/infiltrator-boot/BUILD-INFO"
cat > "$package/DEBIAN/control" <<EOF
Package: infiltrator-boot
Version: ${version}+native1
Section: admin
Priority: optional
Architecture: amd64
Maintainer: Infiltrator Projects
Depends: bash, coreutils, findutils, util-linux
X-Infiltrator-Native-Profile: aggressive
Description: Locally compiled InfiltratorOS UEFI boot manager
 Built from BOOT source for the local processor.
EOF
deb="$work/infiltrator-boot_${version}+native1_amd64.deb"
dpkg-deb --build --root-owner-group "$package" "$deb"
if (( build_only )); then
    install -m 0644 "$deb" "$output_dir/${deb##*/}"
    printf 'Built %s\n' "$output_dir/${deb##*/}"
else
    apt-get -o DPkg::Lock::Timeout=-1 install -y --no-install-recommends "$deb"
fi
exit 0
__BOOT_NATIVE_PAYLOAD__
