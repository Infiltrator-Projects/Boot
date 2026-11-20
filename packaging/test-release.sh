#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Validate downloadable assets without installing BOOT to a machine.
set -euo pipefail
version="${1:?Usage: packaging/test-release.sh VERSION [OUTPUT_DIRECTORY]}"
out="${2:-dist}"
deb="$out/infiltrator-boot_${version}_amd64.deb"
run="$out/BOOT-${version}-amd64.run"
test -s "$deb"
test -s "$run"
test -s "$out/SHA256SUMS"
(cd "$out" && sha256sum -c SHA256SUMS)
test "$(dpkg-deb --field "$deb" Package)" = "infiltrator-boot"
test "$(dpkg-deb --field "$deb" Version)" = "$version"
test "$(dpkg-deb --field "$deb" Architecture)" = "amd64"
# No Debian maintainer scripts: package installation must NOT touch the ESP.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
dpkg-deb --ctrl-tarfile "$deb" | tar -tf - | grep -Eq '^\./control$|^control$'
if dpkg-deb --ctrl-tarfile "$deb" | tar -tf - | grep -Eq '(^|/)(postinst|preinst|prerm|postrm)$'; then
    echo "Unexpected Debian maintainer script" >&2
    exit 1
fi
bash "$run" --extract "$work"
efi="$work/usr/share/infiltrator-boot/boot/boot_x64.efi"
test -s "$efi"
cmp -s boot/boot_x64.efi "$efi"
test -s "$work/usr/share/infiltrator-boot/PREVIEW-NOTICE.txt"
if find "$work" -type f -path '*/drivers_*/*' | grep -q .; then
    echo "Legacy standalone filesystem drivers must not ship" >&2
    exit 1
fi
echo "Verified BOOT AMD64 .deb and self-extracting .run"
