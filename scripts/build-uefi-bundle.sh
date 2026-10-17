#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dist="${1:?dist directory required}"
icons_dir="${2:?generated icons directory required}"
version="${3:-$(tr -d '\r\n' < "$root/VERSION")}"
output="${4:-$dist/Boot-$version-x86_64-uefi.zip}"

boot_efi="$dist/Boot-$version-x86_64.efi"
fs_efi="$dist/InfiltratorFS-$version-x86_64.efi"
for required in \
    "$boot_efi" \
    "$fs_efi" \
    "$dist/boot.conf" \
    "$dist/mb-corpo-s-regular.png" \
    "$dist/mb-corpo-s-bold.png" \
    "$dist/mb-corpo-a-cond-regular.png" \
    "$icons_dir/os_infiltratoros.png"; do
    test -s "$required"
done

test ! -e "$icons_dir/os_refind.png"
test ! -e "$icons_dir/os_refit.png"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
boot_dir="$stage/EFI/BOOT"
mkdir -p "$boot_dir/drivers-disabled" "$boot_dir/icons"

install -m 0644 "$boot_efi" "$boot_dir/BOOTX64.EFI"

# InfiltratorFS is built and published as a separately qualified artifact, but
# Boot's removable-media bundle does not need it for ordinary Linux/Windows EFI
# handoff. Keep it available without placing it in an auto-loaded driver path.
install -m 0644 "$fs_efi" "$boot_dir/drivers-disabled/InfiltratorFS_x64.efi"

install -m 0644 "$dist/boot.conf" "$boot_dir/boot.conf"
install -m 0644 "$dist/mb-corpo-s-regular.png" "$boot_dir/mb-corpo-s-regular.png"
install -m 0644 "$dist/mb-corpo-s-bold.png" "$boot_dir/mb-corpo-s-bold.png"
install -m 0644 "$dist/mb-corpo-a-cond-regular.png" "$boot_dir/mb-corpo-a-cond-regular.png"

# Physical Dell firmware successfully loaded direct PNG assets beside
# BOOTX64.EFI but every inherited icons/<name> lookup fell back to dummy
# graphics. Make the runtime path deterministic by putting the actual menu PNGs
# beside Boot and using icons_dir . in boot.conf. Retain the icons directory as
# provenance/licence material for the shipped artwork.
find "$icons_dir" -maxdepth 1 -type f -name '*.png' -print0 |
while IFS= read -r -d '' icon; do
    install -m 0644 "$icon" "$boot_dir/$(basename "$icon")"
done
cp -R "$icons_dir/." "$boot_dir/icons/"

# Write the ZIP deterministically: sorted paths, fixed timestamps and stable
# permissions. This makes the ready-to-copy UEFI bundle an exact release input
# rather than a runner-dependent archive.
python3 - "$stage" "$output" <<'PY'
from pathlib import Path
import sys
import zipfile

stage = Path(sys.argv[1])
output = Path(sys.argv[2])
output.parent.mkdir(parents=True, exist_ok=True)
required = {
    'EFI/BOOT/BOOTX64.EFI',
    'EFI/BOOT/boot.conf',
    'EFI/BOOT/mb-corpo-s-regular.png',
    'EFI/BOOT/mb-corpo-s-bold.png',
    'EFI/BOOT/mb-corpo-a-cond-regular.png',
    'EFI/BOOT/os_infiltratoros.png',
    'EFI/BOOT/os_infiltrator.png',
    'EFI/BOOT/os_infiltratorboot.png',
    'EFI/BOOT/drivers-disabled/InfiltratorFS_x64.efi',
}
files = sorted(path for path in stage.rglob('*') if path.is_file())
relative = {path.relative_to(stage).as_posix() for path in files}
missing = required - relative
if missing:
    raise SystemExit(f'bundle is missing required files: {sorted(missing)}')

active_driver = 'EFI/BOOT/drivers_x64/InfiltratorFS_x64.efi'
if active_driver in relative:
    raise SystemExit('InfiltratorFS leaked into Boot auto-load path')

for historical in (
    'EFI/BOOT/os_refind.png',
    'EFI/BOOT/os_refit.png',
    'EFI/BOOT/icons/os_refind.png',
    'EFI/BOOT/icons/os_refit.png',
):
    if historical in relative:
        raise SystemExit('historical boot-manager product icon leaked into the Boot runtime bundle')

with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in files:
        rel = path.relative_to(stage).as_posix()
        info = zipfile.ZipInfo(rel, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (0o100644 & 0xFFFF) << 16
        archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)

with zipfile.ZipFile(output, 'r') as archive:
    names = set(archive.namelist())
    missing = required - names
    if missing:
        raise SystemExit(f'written bundle is missing required files: {sorted(missing)}')
    if active_driver in names:
        raise SystemExit('written bundle contains an auto-loaded InfiltratorFS driver')
    bad = archive.testzip()
    if bad is not None:
        raise SystemExit(f'ZIP integrity failure at {bad}')

print(f'{output}: {len(files)} files')
PY
