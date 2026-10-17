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
mkdir -p "$boot_dir/drivers_x64" "$boot_dir/icons"

install -m 0644 "$boot_efi" "$boot_dir/BOOTX64.EFI"
install -m 0644 "$fs_efi" "$boot_dir/drivers_x64/InfiltratorFS_x64.efi"
install -m 0644 "$dist/boot.conf" "$boot_dir/boot.conf"
install -m 0644 "$dist/mb-corpo-s-regular.png" "$boot_dir/mb-corpo-s-regular.png"
install -m 0644 "$dist/mb-corpo-s-bold.png" "$boot_dir/mb-corpo-s-bold.png"
install -m 0644 "$dist/mb-corpo-a-cond-regular.png" "$boot_dir/mb-corpo-a-cond-regular.png"
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
    'EFI/BOOT/icons/os_infiltratoros.png',
    'EFI/BOOT/icons/os_infiltrator.png',
    'EFI/BOOT/icons/os_infiltratorboot.png',
    'EFI/BOOT/drivers_x64/InfiltratorFS_x64.efi',
}
files = sorted(path for path in stage.rglob('*') if path.is_file())
relative = {path.relative_to(stage).as_posix() for path in files}
missing = required - relative
if missing:
    raise SystemExit(f'bundle is missing required files: {sorted(missing)}')
if 'EFI/BOOT/icons/os_refind.png' in relative or 'EFI/BOOT/icons/os_refit.png' in relative:
    raise SystemExit('historical boot-manager product icons leaked into the Boot runtime bundle')

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
    bad = archive.testzip()
    if bad is not None:
        raise SystemExit(f'ZIP integrity failure at {bad}')

print(f'{output}: {len(files)} files')
PY
