#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build an APT-installable Boot payload from the already verified UEFI bundle.
# Neither this script nor the package's installation modifies the EFI partition.
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
bundle="${1:?UEFI bundle path required}"
output="${2:?DEB destination required}"
version="$(tr -d '\r\n' < "$root/VERSION")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
[[ "$output" == *"/infiltrator-boot_${version}_amd64.deb" ]]
test -s "$bundle"
test ! -e "$output"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
pkg="$work/pkg"
mkdir -p "$pkg/DEBIAN" "$pkg/usr/lib/infiltrator-boot/EFI/BOOT/filesystems_x64" "$pkg/usr/sbin"
# Compile native bounded ZIP admission/extraction. No Python packaging runtime.
cxx="${CXX:-c++}"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$root/packaging/extract-bundle.cpp" -larchive -o "$work/extract-bundle"
"$work/extract-bundle" "$bundle" "$pkg/usr/lib/infiltrator-boot"
install -m 0755 "$root/packaging/infiltrator-boot-deploy" "$pkg/usr/sbin/infiltrator-boot-deploy"
install -m 0644 "$root/packaging/infiltrator-boot-deploy-core" "$pkg/usr/lib/infiltrator-boot/deploy-core"
cc -std=c11 -O2 -Wall -Wextra -Werror "$root/packaging/validate-module.c" \
    "$root/refind/so_elf.c" -o "$pkg/usr/lib/infiltrator-boot/validate-module"
printf '%s\n' "$version" > "$pkg/usr/lib/infiltrator-boot/VERSION"
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: infiltrator-boot
Version: $version
Section: admin
Priority: optional
Architecture: amd64
Maintainer: Infiltrator Projects
Depends: bash, coreutils, findutils, util-linux, e2fsprogs, libc6
Description: InfiltratorOS UEFI boot manager and explicitly deployed firmware payload
 This package stages the verified EFI boot manager and support files.
 To provision a fresh /boot/efi/EFI/InfiltratorBoot installation, run
 infiltrator-boot-deploy --install-new --confirm as root.
 To update an existing installation and all managed graphics, run
 infiltrator-boot-deploy --update-existing --confirm as root.
 Installing the Debian package itself never modifies the ESP or firmware.
CONTROL
mkdir -p "$(dirname "$output")"
dpkg-deb --build --root-owner-group "$pkg" "$output"
test -s "$output"
test "$(dpkg-deb -f "$output" Package)" = infiltrator-boot
test "$(dpkg-deb -f "$output" Version)" = "$version"
test "$(dpkg-deb -f "$output" Architecture)" = amd64
check="$work/check"
mkdir -p "$check"
dpkg-deb -x "$output" "$check"
cmp "$pkg/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI" "$check/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI"
cmp "$root/packaging/infiltrator-boot-deploy-core" "$check/usr/lib/infiltrator-boot/deploy-core"
cmp "$root/packaging/infiltrator-boot-deploy" "$check/usr/sbin/infiltrator-boot-deploy"
echo "Built independently installable $output with no ESP mutation."
