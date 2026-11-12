#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
deb=${1:?Verified BOOT package required}
output=${2:?Native installer destination required}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/input/source" "$work/extracted"
git -C "$root" archive HEAD VERSION Make.common Makefile refind EfiLib gzip libeg mok include \
    refind-sbat.csv packaging/infiltrator-boot-deploy-core packaging/infiltrator-boot-deploy \
    LICENSE.txt COPYING.txt CREDITS.txt | tar -xf - -C "$work/input/source"
dpkg-deb -x "$deb" "$work/extracted"
cp -a "$work/extracted/usr/lib/infiltrator-boot" "$work/input/resources"
rm -f "$work/input/resources/EFI/BOOT/BOOTX64.EFI" "$work/input/resources/deploy-core" "$work/input/resources/VERSION"
test -s "$work/input/resources/EFI/BOOT/boot.conf"
test ! -e "$work/input/resources/EFI/BOOT/BOOTX64.EFI"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -czf "$work/payload.tar.gz" -C "$work/input" .
digest=$(sha256sum "$work/payload.tar.gz" | cut -d' ' -f1)
mkdir -p -- "$(dirname -- "$output")"
sed "s/@PAYLOAD_SHA256@/$digest/" "$root/packaging/native-installer.sh" > "$output"
cat "$work/payload.tar.gz" >> "$output"
chmod 0755 "$output"
