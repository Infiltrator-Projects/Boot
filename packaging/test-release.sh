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

# The downloaded RUN is intentionally a small script, not a source archive.
# Its ONLY operation downloads the pinned source, recompiles, and calls the
# existing boot-install. Never execute it in a GitHub runner.
bash -n "$run"
# Validate the original installer that the RUN fetches and executes.
bash -n boot-install
grep -Fq 'InstallationFailed=1' boot-install
grep -Fq 'exit "$InstallationFailed"' boot-install
grep -Fxq 'TargetDir=/EFI/InfiltratorOS/BOOT' boot-install
grep -Fxq 'TargetX64="BOOTX64.EFI"' boot-install
grep -Fq 'GuardDefaultBootLocation || exit 1' boot-install
grep -Fq 'Boot="$TargetX64"' boot-install

# A machine without a usable ESP must fail, not report installation success.
grep -A5 'create an ESP, and ideally boot in EFI mode, before installing BOOT.' boot-install |
    grep -Fq 'exit 1'
test "$(head -n 1 "$run")" = '#!/usr/bin/env bash'
test "$(stat -c '%s' "$run")" -lt 16384
expected=$(git rev-parse --verify HEAD)
grep -Fxq "SOURCE_SHA=\"$expected\"" "$run"
grep -Fq "SOURCE_URL='https://github.com/Infiltrator-Projects/Boot.git'" "$run"
grep -Fq 'fetch --quiet --depth 1 --filter=blob:none origin "$SOURCE_SHA"' "$run"
grep -Fq 'rev-parse HEAD) == "$SOURCE_SHA"' "$run"
grep -Fq -- '-march=native -mtune=native -mgeneral-regs-only' "$run"
grep -Fq 'bash ./boot-install --nodrivers' "$run"
if grep -Eq -- '--build-only|--extract|--install|__BOOT_SOURCE_ARCHIVE_BELOW__|SOURCE_DIGEST=|base64 --decode' "$run"; then
    echo "Native RUN has an unwanted mode or embedded source archive" >&2
    exit 1
fi
if [[ $(grep -Ec '^bash \./boot-install --nodrivers$' "$run") != 1 ]]; then
    echo "Native BOOT installer must invoke the original installer exactly once" >&2
    exit 1
fi
echo "Verified compact source-downloading native BOOT RUN and generic DEB"
