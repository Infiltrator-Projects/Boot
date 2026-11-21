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
grep -Fq 'usr/share/infiltrator-boot/docs/COPYING.txt' "$work/package-files.txt"
if grep -Eq 'usr/share/infiltrator-boot/(mvboot|boot-mkdefault)( |$)' "$work/package-files.txt"; then
    echo "Obsolete or Python-dependent helper was included" >&2; exit 1
fi
if grep -Eq 'usr/share/infiltrator-boot/docs/(LICENSE|CREDITS|NEWS)\.txt' "$work/package-files.txt"; then
    echo "Obsolete upstream text file was included in the BOOT package" >&2
    exit 1
fi
dpkg-deb --ctrl-tarfile "$deb" | tar -tf - > "$work/control.txt"
grep -Eq '^\./control$|^control$' "$work/control.txt"
if grep -Eq '(^|/)(postinst|preinst|prerm|postrm)$' "$work/control.txt"; then
    echo "Generic package unexpectedly runs firmware installation scripts" >&2
    exit 1
fi

# Test the one maintained RUN file, not an embedded or duplicated installer.
bash -n "$run"
bash -n boot-native-installer.run
test "$(head -n 1 "$run")" = '#!/usr/bin/env bash'
test "$(stat -c '%s' "$run")" -ge 50000
test "$(stat -c '%s' "$run")" -lt 200000

expected=$(git rev-parse --verify HEAD)
sed -e "s/__BOOT_SOURCE_COMMIT__/$expected/g" \
    -e "s/__BOOT_RELEASE_VERSION__/$version/g" \
    boot-native-installer.run > "$work/expected.run"
cmp "$work/expected.run" "$run"

grep -Fxq "SOURCE_SHA=\"$expected\"" "$run"
grep -Fxq "RELEASE_VERSION=\"$version\"" "$run"
grep -Fq -- '-DBOOT_RELEASE_VERSION=' "$run"
grep -Fq 'BOOT_SBAT_CSV=boot-sbat-release.csv' "$run"
grep -Fq "SOURCE_URL='https://github.com/Infiltrator-Projects/Boot.git'" "$run"
grep -Fq 'fetch --quiet --depth 1 --filter=blob:none origin "$SOURCE_SHA"' "$run"
grep -Fq 'rev-parse HEAD) == "$SOURCE_SHA"' "$run"
grep -Fq -- '-march=native -mtune=native -mgeneral-regs-only' "$run"
grep -Fq 'export BOOT_INSTALL_SOURCE_DIR="$source_dir"' "$run"
grep -Fq 'set -- --nodrivers' "$run"
grep -Fxq 'TargetDir=/EFI/InfiltratorOS/BOOT' "$run"
grep -Fxq 'TargetX64="BOOTX64.EFI"' "$run"
grep -Fq 'rm -rf -- "$InstallDir/$TargetDir"' "$run"
grep -Fq 'GuardDefaultBootLocation || exit 1' "$run"
grep -Fq 'FindLinuxESP() {' "$run"
grep -Fq 'FindMountedESP() {' "$run"
grep -Fq 'AddBootEntry() {' "$run"
grep -Fq 'GenerateBootLinuxConf() {' "$run"
grep -Fxq 'exit "$InstallationFailed"' "$run"
test "$(grep -Ec '^GetParams "\$@"$' "$run")" -eq 1
test "$(grep -Ec '^InstallOnLinux\(\) \{' "$run")" -eq 1
! grep -Fq 'embedded_boot_install' "$run"
! grep -Fq 'SetVarsForBoot() {' "$run"
! grep -Fq 'SetVarsForMsBoot() {' "$run"
! grep -Fq 'MountDefaultTarget() {' "$run"
! grep -Fq 'cat boot-install' packaging/build-release.sh
grep -Fq 'if (($# != 0)); then' "$run"
grep -Fq 'The only permitted destination is /EFI/InfiltratorOS/BOOT.' "$run"
echo "Verified one native RUN program and original installation behaviour"
