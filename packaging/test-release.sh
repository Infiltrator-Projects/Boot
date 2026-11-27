#!/usr/bin/env bash
# BOOT / Release contract tests
#
# Checks the package contents, pinned installer source and fixed installation
# rules against staged release artifacts.
#
# These checks do not establish physical firmware bootability or the safety
# of every malformed input.

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
[[ $(wc -l < fonts/SHA256SUMS) -eq 23 ]] || exit 1
while read -r digest face; do
    grep -Fq "/boot/fonts/$face" "$work/package-files.txt" || {
        echo "Missing packaged MB/Daimler font: $face" >&2
        exit 1
    }
done < fonts/SHA256SUMS
grep -Fq 'usr/share/infiltrator-boot/boot/boot_x64.efi' "$work/package-files.txt"
# Documentation is now part of the staged package but is not an installer.
# These assertions catch stale packaging loops that silently skip new Markdown.
for document in README.md docs/ARCHITECTURE.md docs/HARDWARE.md docs/PHILOSOPHY.md docs/VALIDATION.md docs/CHANGELOG.md; do
    grep -Fq "usr/share/infiltrator-boot/$document" "$work/package-files.txt" || {
        echo "Missing maintained documentation: $document" >&2; exit 1;
    }
done
for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    grep -Fq "usr/share/infiltrator-boot/boot/drivers_x64/${filesystem}_x64.efi" "$work/package-files.txt" || exit 1
done
if grep -Eq 'usr/share/infiltrator-boot/(mvboot|boot-mkdefault)( |$)' "$work/package-files.txt"; then
    echo "Obsolete or Python-dependent helper was included" >&2; exit 1
fi
if grep -Eq 'usr/share/infiltrator-boot/docs/[^[:space:]]+\.txt' "$work/package-files.txt"; then
    echo "Obsolete upstream text file was included in the BOOT package" >&2
    exit 1
fi
dpkg-deb --ctrl-tarfile "$deb" | tar -tf - > "$work/control.txt"
grep -Eq '^\./control$|^control$' "$work/control.txt"
grep -Eq '^\./postinst$|^postinst$' "$work/control.txt"
if grep -Eq '(^|/)(preinst|prerm|postrm)$' "$work/control.txt"; then
    echo "Unexpected additional package maintainer scripts" >&2
    exit 1
fi
dpkg-deb -e "$deb" "$work/control"
dpkg-deb -x "$deb" "$work/unpacked"
test -x "$work/control/postinst"
grep -Fq 'exec /usr/lib/infiltrator-boot/install-generic.sh' "$work/control/postinst"
test -s "$work/unpacked/usr/lib/infiltrator-boot/install-generic.sh"
bash -n "$work/unpacked/usr/lib/infiltrator-boot/install-generic.sh"
grep -Fq 'Depends: bash, efibootmgr' <(dpkg-deb -f "$deb")
bash tests/deb-install-safety.sh "$deb"

test -s themes/boot-luxury.png
for asset in boot-luxury.png boot-logo.png boot-mark.png boot-underline.png; do
    test -s "themes/$asset"
    grep -Fq "usr/share/infiltrator-boot/boot/themes/$asset" "$work/package-files.txt"
    test -s "$work/unpacked/usr/share/infiltrator-boot/boot/themes/$asset"
    cmp -s "themes/$asset" "$work/unpacked/usr/share/infiltrator-boot/boot/themes/$asset" || { echo "Packaged theme differs from Git source: $asset" >&2; exit 1; }
done
! grep -Eq 'drawTwinSlash|[Ss][Vv][Gg]' boot/theme.c
! grep -Eq 'boot-luxury|boot-mark|boot-underline' boot/theme.c
for option in theme_background theme_logo theme_mark theme_underline; do
    grep -Fq "$option themes/" boot.conf-sample
    grep -Fq "L\"$option\"" boot/config.c
done
grep -Fq 'theme_background themes/boot-luxury.png' boot.conf-sample
grep -Fq 'theme_logo themes/boot-logo.png' boot.conf-sample
grep -Fq 'ThemeBackgroundFileName' boot/theme.c
grep -Fq 'ThemeMarkFileName' boot/theme.c
grep -Fq 'ThemeUnderlineFileName' boot/theme.c
bash -n "$run"
bash -n boot-native-installer.run
test "$(head -n 1 "$run")" = '#!/usr/bin/env bash'
test "$(stat -c '%s' "$run")" -ge 40000
test "$(stat -c '%s' "$run")" -lt 200000
# Reconstruct the published installer from the tracked template and the two
# release substitutions. Byte comparison catches changes elsewhere in the
# script that individual checks for commands or filenames could miss.
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
grep -Fq 'set -- --alldrivers' "$run"
! grep -Fq 'set -- --nodrivers' "$run"
grep -Fq 'sparse-checkout set boot EfiLib gzip libeg mok include icons fonts banners themes keys filesystems' "$run"
grep -Fxq 'TargetDir=/EFI/InfiltratorOS/BOOT' "$run"
grep -Fxq 'TargetX64="BOOTX64.EFI"' "$run"
# rEFInd-style Secure Boot support is a package-installer responsibility:
# the RUN must find a host Shim, sign its BOOT stage and refuse to replace a
# working BootOrder until the local signing MOK is enrolled.
grep -Fq 'set -- --alldrivers --shim "$BootShim" --keepname --localkeys --yes' "$run"
grep -Fq 'mokutil --test-key "$DerKey"' "$run"
grep -Fq 'sbverify --cert "$CertKey"' "$run"
grep -Fq 'BootSecureBootEnabled=$(od -An -tu1 -j4 -N1' "$run"
! grep -Fq 'BOOT Boot Manager (direct)' "$run"
grep -Fq 'rm -rf -- "$InstallDir/$TargetDir"' "$run"
if grep -Eq 'boot_v2_(stage_modules|install_staged)|BOOT_V2_STAGE|/System/Filesystems|common-v2-verify' "$run"; then
    echo 'Native installer must not install independent filesystem modules' >&2
    exit 1
fi
if grep -Ei '(/System/Filesystems/|/[^/[:space:]]+\.so([[:space:]]|$)|common-v2-(install\.sh|verify)([[:space:]]|$))' "$work/package-files.txt"; then
    echo 'Generic package must not contain independent filesystem modules or their installers' >&2
    exit 1
fi
! grep -Fq "BootFileHash" "$run"
! grep -Fq ".infiltrator-boot-loader.sha256" "$run"
grep -Fq 'CheckBootDestination || exit 1' "$run"
grep -Fq 'FindLinuxESP() {' "$run"
grep -Fq 'FindMountedESP() {' "$run"
grep -Fq 'AddBootEntry() {' "$run"
! grep -Fq 'GenerateBootLinuxConf() {' "$run"
! grep -Fq 'GenerateBootLinuxConf' "$run"
! grep -Fq 'RLConfFile=' "$run"
! grep -Fq 'InstallDir/EFI/tools' "$run"
grep -Fq 'Installing BOOT from a Linux host into /EFI/InfiltratorOS/BOOT' "$run"
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
echo "Verified generic DEB and native RUN installation behaviour"
