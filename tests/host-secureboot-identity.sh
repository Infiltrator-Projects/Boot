#!/usr/bin/env bash
# Secure Boot identity discovery regression tests. These use disposable keys,
# mock MOK status and a disposable ESP: no actual firmware is modified.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

awk '/^RootDir=/ {inside=1} /^GetParams "\$@"$/ {exit} inside {print}' \
    boot-native-installer.run > "$work/installer-functions.sh"
bash -n "$work/installer-functions.sh"
source "$work/installer-functions.sh"

mkdir -p "$work/roots/existing" "$work/roots/other" "$work/esp/EFI/InfiltratorOS/BOOT"
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$work/roots/existing/boot.key" \
    -out "$work/roots/existing/boot.crt" \
    -days 1 -subj '/CN=BOOT test certificate/' >/dev/null 2>&1
openssl x509 -in "$work/roots/existing/boot.crt" \
    -outform DER -out "$work/roots/existing/boot.cer"
original_digest=$(sha256sum "$work/roots/existing/boot.key" | cut -d ' ' -f1)

mokutil() {
    [[ "$1" == --test-key ]] || return 93
    if [[ "$TEST_MOK_STATE" == enrolled ]]; then
        echo "$2 is already enrolled"
        return 1
    fi
    echo "$2 is not enrolled"
    return 0
}

# Existing enrolled keypair is reused without duplicating its private key.
TEST_MOK_STATE=enrolled
DiscoverEnrolledSigningIdentity "$work/roots/existing"
test "${BOOT_SIGNING_IDENTITY_SELECTED:-0}" = 1
test "$PrivateKey" = "$work/roots/existing/boot.key"
test -s "$CertKey"
test -s "$DerKey"
EtcKeysDir="$work/unused"
LocalKeysBase=boot_local
GenerateKeys
test "$PrivateKey" = "$work/roots/existing/boot.key"
test "$(sha256sum "$PrivateKey" | cut -d ' ' -f1)" = "$original_digest"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
LocalKeys=1
CopyKeys
test -s "$InstallDir/$TargetDir/keys/boot_local.cer"
test -s "$InstallDir/$TargetDir/keys/boot_local.crt"
test ! -e "$InstallDir/$TargetDir/keys/boot_local.key"
test ! -e "$work/unused/boot_local.key"

# An unenrolled pair must not be treated as trusted or selected.
unset BOOT_SIGNING_IDENTITY_SELECTED PrivateKey CertKey DerKey
TEST_MOK_STATE=unenrolled
DiscoverEnrolledSigningIdentity "$work/roots/existing"
test "${BOOT_SIGNING_IDENTITY_SELECTED:-0}" != 1

# Multiple distinct enrolled signing identities require explicit handling
# rather than silently selecting an arbitrary certificate and private key.
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$work/roots/other/other.key" \
    -out "$work/roots/other/other.crt" \
    -days 1 -subj '/CN=Other BOOT test certificate/' >/dev/null 2>&1
TEST_MOK_STATE=enrolled
if DiscoverEnrolledSigningIdentity "$work/roots"; then
    echo "Ambiguous enrolled credentials were accepted" >&2
    exit 1
fi
echo "BOOT Secure Boot identity discovery and public-only ESP placement passed"
