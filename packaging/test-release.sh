#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Qualify the generic DEB and the source-bearing native RUN without deploying.
set -euo pipefail
version="$1"
out="$2"
deb="$out/infiltrator-boot_$version\_amd64.deb"
run="$out/boot-$version-native-installer.run"
test -s "$deb"
test -s "$run"
test -s "$out/SHA256SUMS"
(cd "$out" && sha256sum -c SHA256SUMS)
test "$(dpkg-deb --field "$deb" Package)" = "infiltrator-boot"
test "$(dpkg-deb --field "$deb" Version)" = "$version"
test "$(dpkg-deb --field "$deb" Architecture)" = "amd64"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
dpkg-deb --contents "$deb" > "$work/package-list.txt"
grep -F "usr/share/infiltrator-boot/boot/boot_x64.efi" "$work/package-list.txt" > /dev/null
dpkg-deb --ctrl-tarfile "$deb" | tar -tf - > "$work/control-list.txt"
grep -Eq "^\./control$|^control$" "$work/control-list.txt"
if grep -Eq "(^|/)(postinst|preinst|prerm|postrm)$" "$work/control-list.txt"; then
    echo "The generic Debian package must not run a boot installer automatically" >&2
    exit 1
fi

# Read the source archive directly. DO NOT invoke the RUN in CI: its one
# supported operation is to compile and install BOOT with boot-install.
archive_line=$(LC_ALL=C awk '/^__BOOT_SOURCE_ARCHIVE_BELOW__$/ { print NR + 1; exit }' "$run")
[[ -n $archive_line ]] || { echo "Native RUN has no source archive" >&2; exit 1; }
sed -n "1,$((archive_line - 2))p" "$run" > "$work/native-wrapper.sh"
bash -n "$work/native-wrapper.sh"
grep -Fq 'make CFLAGS=' "$work/native-wrapper.sh"
grep -Fq 'bash ./boot-install --nodrivers "$@"' "$work/native-wrapper.sh"
if grep -Eq -- '--build-only|--extract|--install' "$work/native-wrapper.sh"; then
    echo "Native RUN contains an unwanted alternate mode" >&2
    exit 1
fi
mkdir "$work/source"
tail -n +"$archive_line" "$run" | tar -xz -C "$work/source"
test -s "$work/source/boot/main.c"
test -s "$work/source/Make.common"
test -s "$work/source/boot-install"
test -s "$work/source/boot.conf-sample"
test ! -f "$work/source/boot/boot_x64.efi"
# The source bundle must actually compile on a clean machine, not simply
# carry a copied EFI image from the GitHub runner.
make -C "$work/source" CFLAGS="-O2 -march=native -mtune=native -mgeneral-regs-only -fno-stack-protector -fshort-wchar -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Wall" gnuefi
test -s "$work/source/boot/boot_x64.efi"
file "$work/source/boot/boot_x64.efi" | grep -Fq 'PE32+'

if find "$work/source" -type f -path '*/drivers_*/*.efi' | grep -q .; then
    echo "Standalone filesystem EFI driver binaries must not be bundled" >&2
    exit 1
fi
echo "Verified generic BOOT DEB and native-source RUN (existing boot-install, no alternate mode)"
