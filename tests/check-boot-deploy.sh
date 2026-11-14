#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname "$0")/.." && pwd)
core="$root/packaging/infiltrator-boot-deploy-core"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
payload="$work/package/EFI/BOOT"
esp="$work/esp"
mkdir -p "$payload/icons" "$esp/EFI" "$work/original"
for name in BOOTX64.EFI boot.conf mb-corpo-s-regular.png mb-corpo-s-bold.png mb-corpo-a-cond-regular.png boot-banner.png boot-selection-big.png boot-selection-small.png; do
    printf 'original %s\n' "$name" > "$payload/$name"
done
printf 'icon 1\n' > "$payload/icons/windows.png"
printf 'icon 2\n' > "$payload/icons/linux.png"
printf 'old firmware entry should be unchanged\n' > "$esp/EFI/BOOTX64.EFI"

# Fresh installation is a complete resource copy, not just an executable.
bash "$core" --install-new "$payload" "$esp"
target="$esp/EFI/InfiltratorBoot"
for name in BOOTX64.EFI boot.conf mb-corpo-s-regular.png mb-corpo-s-bold.png mb-corpo-a-cond-regular.png boot-banner.png boot-selection-big.png boot-selection-small.png; do
    cmp "$payload/$name" "$target/$name"
done
cmp "$payload/icons/linux.png" "$target/icons/linux.png"
[[ ! -e $target/drivers_x64 && -d $target/filesystems_x64 && -z $(find "$esp/EFI" -type l -print -quit) ]]
[[ $(cat "$esp/EFI/BOOTX64.EFI") == 'old firmware entry should be unchanged' ]]
if bash "$core" --install-new "$payload" "$esp" > "$work/double-install.log" 2>&1; then
    echo 'Second new installation was incorrectly allowed' >&2; exit 1
fi

# Updating keeps the exact local configuration and extra assets.
printf 'local user config\n' > "$target/boot.conf"
mkdir "$target/drivers_x64"
printf 'independent OFS driver\n' > "$target/drivers_x64/OFS.EFI"
printf 'custom artwork\n' > "$target/icons/personal.png"
printf 'next boot executable\n' > "$payload/BOOTX64.EFI"
printf 'new icon\n' > "$payload/icons/windows.png"
printf 'new default config\n' > "$payload/boot.conf"
expect_migration_skip() {
    local result=0
    bash "$core" --update-existing "$payload" "$esp" > "$work/blocked.log" 2>&1 || result=$?
    [[ $result == 77 ]]
    [[ $(cat "$target/BOOTX64.EFI") == 'original BOOTX64.EFI' ]]
    [[ ! -e "$esp/EFI/.InfiltratorBoot.rollback" ]]
}
expect_migration_skip
[[ $(cat "$target/drivers_x64/OFS.EFI") == 'independent OFS driver' ]]
mv "$target/drivers_x64/OFS.EFI" "$work/original/"
printf '\xef\xbb\xbf  "scan_driver_dirs" EFI/other\n' > "$target/boot.conf"
expect_migration_skip
printf 'include other.conf\n' > "$target/boot.conf"
expect_migration_skip
printf 'i\0n\0c\0l\0u\0d\0e\0\n\0' > "$target/boot.conf"
expect_migration_skip
printf 'local user config\n' > "$target/boot.conf"
bash "$core" --update-existing "$payload" "$esp"
[[ $(cat "$target/boot.conf") == 'local user config' ]]
[[ $(cat "$target/boot.conf.package-default") == 'new default config' ]]
[[ $(cat "$target/icons/personal.png") == 'custom artwork' ]]
cmp "$payload/BOOTX64.EFI" "$target/BOOTX64.EFI"
cmp "$payload/icons/windows.png" "$target/icons/windows.png"
[[ $(cat "$esp/EFI/.InfiltratorBoot.rollback/BOOTX64.EFI") == 'original BOOTX64.EFI' ]]

# Explicit rollback restores the full prior tree, not only the EFI binary.
bash "$core" --recover "$payload" "$esp"
[[ $(cat "$target/BOOTX64.EFI") == 'original BOOTX64.EFI' ]]
[[ $(cat "$target/boot.conf") == 'local user config' ]]
[[ ! -d "$esp/EFI/.InfiltratorBoot.rollback" ]]

# Bad input must not affect an existing installation.
cp "$payload/BOOTX64.EFI" "$work/good.efi"
rm "$payload/BOOTX64.EFI"
if bash "$core" --update-existing "$payload" "$esp" > "$work/invalid.log" 2>&1; then
    echo 'Invalid package was accepted' >&2; exit 1
fi
[[ $(cat "$target/BOOTX64.EFI") == 'original BOOTX64.EFI' ]]
cp "$work/good.efi" "$payload/BOOTX64.EFI"
ln -s /etc/passwd "$payload/icons/forbidden.png"
if bash "$core" --update-existing "$payload" "$esp" > "$work/link.log" 2>&1; then
    echo 'Payload symlink was accepted' >&2; exit 1
fi
rm "$payload/icons/forbidden.png"

# Simulate a crash between renaming old directory and staging new directory.
mv "$target" "$esp/EFI/.InfiltratorBoot.rollback"
if bash "$core" --install-new "$payload" "$esp" > "$work/crash.log" 2>&1; then
    echo 'Interrupted swap was treated as fresh install' >&2; exit 1
fi
bash "$core" --recover "$payload" "$esp"
[[ -f "$target/BOOTX64.EFI" && ! -e "$esp/EFI/.InfiltratorBoot.rollback" ]]

# A write/rename failure in the critical swap window must restore the previous
# installation rather than leave the firmware path missing.
mockbin="$work/fault-bin"
mkdir -p "$mockbin"
cat > "$mockbin/mv" <<'MV'
#!/usr/bin/env bash
for arg in "$@"; do
    if [[ "$arg" == *"/.InfiltratorBoot.pending."* ]]; then
        echo 'simulated promotion failure' >&2
        exit 99
    fi
done
exec /usr/bin/mv "$@"
MV
chmod +x "$mockbin/mv"
if PATH="$mockbin:$PATH" bash "$core" --update-existing "$payload" "$esp" > "$work/swapfail.log" 2>&1; then
    echo 'Simulated deployment failure unexpectedly succeeded' >&2; exit 1
fi
[[ $(cat "$target/BOOTX64.EFI") == 'original BOOTX64.EFI' ]]
[[ ! -e "$esp/EFI/.InfiltratorBoot.rollback" ]]

# A genuinely empty FAT ESP can receive its first EFI directory.
blank="$work/blank-esp"
mkdir -p "$blank"
bash "$core" --install-new "$payload" "$blank"
[[ -f "$blank/EFI/InfiltratorBoot/BOOTX64.EFI" ]]

# The active legacy EXT4 driver is retired only when its V2 replacement
# passes validation. Recovery retains the previous full deployment.
mkdir "$work/engine"
cp "$core" "$work/engine/deploy-core"
cat > "$work/engine/validate-module" <<'VALIDATE'
#!/bin/sh
test "$(cat -- "$1")" = 'qualified fixture'
VALIDATE
chmod +x "$work/engine/validate-module"
module="$work/EXT4.so"
printf 'old EXT4 EFI reader\n' > "$target/drivers_x64/ext4_x64.efi"
expect_migration_skip
printf 'invalid fixture\n' > "$module"
if bash "$work/engine/deploy-core" --update-existing "$payload" "$esp" "$module"; then
    echo 'Upgrade accepted an invalid EXT4 module' >&2; exit 1
fi
grep -Fxq 'old EXT4 EFI reader' "$target/drivers_x64/ext4_x64.efi"
[[ ! -e "$esp/EFI/.InfiltratorBoot.rollback" ]]
printf 'qualified fixture\n' > "$module"
bash "$work/engine/deploy-core" --update-existing "$payload" "$esp" "$module"
cmp "$module" "$target/filesystems_x64/EXT4.so"
cmp "$payload/BOOTX64.EFI" "$target/BOOTX64.EFI"
[[ ! -e "$target/drivers_x64/ext4_x64.efi" ]]
grep -Fxq 'old EXT4 EFI reader' "$esp/EFI/.InfiltratorBoot.rollback/drivers_x64/ext4_x64.efi"
grep -Fxq 'local user config' "$target/boot.conf"

# More than eight valid providers must all be staged, while a bad late
# provider must leave the current installation untouched.
mkdir "$work/providers"
providers=()
for index in $(seq 1 24); do
    provider="$work/providers/Provider${index}.so"
    printf 'qualified fixture\n' > "$provider"
    providers+=("$provider")
done
bash "$work/engine/deploy-core" --update-existing "$payload" "$esp" "${providers[@]}"
for provider in "${providers[@]}"; do
    cmp "$provider" "$target/filesystems_x64/${provider##*/}"
done
printf 'invalid fixture\n' > "${providers[23]}"
if bash "$work/engine/deploy-core" --update-existing "$payload" "$esp" "${providers[@]}"; then
    echo 'Upgrade accepted an invalid later provider' >&2; exit 1
fi
grep -Fxq 'qualified fixture' "$target/filesystems_x64/Provider24.so"
cmp "$payload/BOOTX64.EFI" "$target/BOOTX64.EFI"

# Existing non-BOOT firmware path never changed during any operation.
[[ $(cat "$esp/EFI/BOOTX64.EFI") == 'old firmware entry should be unchanged' ]]
! find "$esp/EFI" -maxdepth 1 -name '.InfiltratorBoot.pending.*' | grep -q .
echo 'BOOT-DEPLOY-FRESH-UPDATE-PRESERVE-RECOVERY-PASS'
