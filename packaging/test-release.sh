#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify native RUN metadata and generic package without running boot-install.
set -euo pipefail

version="${1:?Usage: packaging/test-release.sh VERSION [OUTPUT_DIRECTORY]}"
out="${2:-dist}"
deb="$out/infiltrator-boot_${version}_amd64.deb"
run="$out/boot-$version-native-installer.run"

test -s "$deb"
test -s "$run"
test -s "$out/SHA256SUMS"
(cd "$out" && sha256sum -c SHA256SUMS)
test "$(dpkg-deb -f "$deb" Package)" = infiltrator-boot
test "$(dpkg-deb -f "$deb" Version)" = "$version"
test "$(dpkg-deb -f "$deb" Architecture)" = amd64

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
dpkg-deb --contents "$deb" > "$work/package-files.txt"
grep -Fq 'usr/share/infiltrator-boot/boot/boot_x64.efi' "$work/package-files.txt"
dpkg-deb --ctrl-tarfile "$deb" | tar -tf - > "$work/control.txt"
grep -Eq '^\./control$|^control$' "$work/control.txt"
if grep -Eq '(^|/)(postinst|preinst|prerm|postrm)$' "$work/control.txt"; then
    echo "Generic package unexpectedly runs firmware installation scripts" >&2
    exit 1
fi

# The ONE distributed RUN contains both complete source and boot-install.
# Verify archive integrity and source identity without touching firmware.
bash -n "$run"
test "$(head -n 1 "$run")" = '#!/usr/bin/env bash'
expected=$(git rev-parse --verify HEAD)
grep -Fxq "SOURCE_SHA=\"$expected\"" "$run"
grep -Fq "SOURCE_DIGEST=" "$run"
grep -Fq -- '-march=native -mtune=native -mgeneral-regs-only' "$run"
grep -Fq 'bash ./boot-install --nodrivers' "$run"
if grep -Eq -- '--build-only|git -C .* fetch|SOURCE_URL=' "$run"; then
    echo "Native BOOT RUN must install from its embedded source, not download or build-only" >&2
    exit 1
fi
awk '$0 == "__BOOT_SOURCE_ARCHIVE_BELOW__" { copy=1; next }
     $0 == "__BOOT_SOURCE_ARCHIVE_END__" { exit }
     copy { print }' "$run" | base64 --decode > "$work/bundled.tar.gz"
digest=$(sha256sum "$work/bundled.tar.gz" | cut -d ' ' -f 1)
grep -Fxq "SOURCE_DIGEST=\"$digest\"" "$run"
mkdir "$work/source"
tar -xzf "$work/bundled.tar.gz" -C "$work/source" --no-same-owner --no-same-permissions
test -s "$work/source/boot-install"
cmp boot-install "$work/source/boot-install"
bash -n "$work/source/boot-install"
grep -Fxq 'TargetDir=/EFI/InfiltratorOS/BOOT' "$work/source/boot-install"
grep -Fxq 'TargetX64="BOOTX64.EFI"' "$work/source/boot-install"
grep -Fq 'GuardDefaultBootLocation || exit 1' "$work/source/boot-install"
grep -Fq 'Boot="$TargetX64"' "$work/source/boot-install"
! grep -Fq 'TargetDir=/EFI/InfiltratorBoot' "$work/source/boot-install"
grep -Fq 'InstallationFailed=1' "$work/source/boot-install"
grep -Fq 'exit "$InstallationFailed"' "$work/source/boot-install"
grep -A5 'create an ESP, and ideally boot in EFI mode, before installing BOOT.' "$work/source/boot-install" |
    grep -Fq 'exit 1'
test -s "$work/source/Makefile"
test -s "$work/source/boot/main.c"
echo "Verified self-contained native BOOT RUN, bundled installer and generic DEB"
