#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dist="${1:?dist directory required}"
icons_dir="${2:?generated icons directory required}"
version="${3:-$(tr -d '\r\n' < "$root/VERSION")}"
output="${4:-$dist/Boot-$version-x86_64-uefi.zip}"

boot_efi="$dist/Boot-$version-x86_64.efi"
for required in \
    "$boot_efi" \
    "$dist/boot.conf" \
    "$dist/mb-corpo-s-regular.png" \
    "$dist/mb-corpo-s-bold.png" \
    "$dist/mb-corpo-a-cond-regular.png" \
    "$dist/boot-banner.png" \
    "$dist/boot-selection-big.png" \
    "$dist/boot-selection-small.png" \
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

install -m 0644 "$dist/boot.conf" "$boot_dir/boot.conf"
install -m 0644 "$dist/mb-corpo-s-regular.png" "$boot_dir/mb-corpo-s-regular.png"
install -m 0644 "$dist/mb-corpo-s-bold.png" "$boot_dir/mb-corpo-s-bold.png"
install -m 0644 "$dist/mb-corpo-a-cond-regular.png" "$boot_dir/mb-corpo-a-cond-regular.png"
install -m 0644 "$dist/boot-banner.png" "$boot_dir/boot-banner.png"
install -m 0644 "$dist/boot-selection-big.png" "$boot_dir/boot-selection-big.png"
install -m 0644 "$dist/boot-selection-small.png" "$boot_dir/boot-selection-small.png"

# The reader opens icons/ and reads leaf filenames through its directory handle.
# Ship one copy of each icon in that directory, matching boot.conf.
find "$icons_dir" -maxdepth 1 -type f -name '*.png' -print0 |
while IFS= read -r -d '' icon; do
    install -m 0644 "$icon" "$boot_dir/icons/$(basename "$icon")"
done
# Retain artwork attribution and licences without a second copy of the graphics.
install -m 0644 "$icons_dir/README" "$boot_dir/icons/README"
if [[ -d "$icons_dir/licenses" ]]; then
    cp -R "$icons_dir/licenses" "$boot_dir/icons/licenses"
fi

# Write the ZIP deterministically: sorted paths, fixed timestamps and stable
# permissions. This makes the ready-to-copy UEFI bundle an exact release input.
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
    'EFI/BOOT/boot-banner.png',
    'EFI/BOOT/boot-selection-big.png',
    'EFI/BOOT/boot-selection-small.png',
    'EFI/BOOT/icons/os_infiltratoros.png',
    'EFI/BOOT/icons/os_infiltrator.png',
    'EFI/BOOT/icons/os_infiltratorboot.png',
    'EFI/BOOT/icons/func_reset.png',
    'EFI/BOOT/icons/func_shutdown.png',
    'EFI/BOOT/icons/func_firmware.png',
    'EFI/BOOT/icons/os_uefi.png',
    'EFI/BOOT/icons/os_unknown.png',
}
files = sorted(path for path in stage.rglob('*') if path.is_file())
relative = {path.relative_to(stage).as_posix() for path in files}
root_graphics = {name for name in required
                 if name.endswith('.png') and Path(name).parent == Path('EFI/BOOT')}
graphics = {name for name in relative if name.lower().endswith('.png')}
misplaced_pngs = sorted(name for name in graphics
                        if name not in root_graphics
                        and Path(name).parent != Path('EFI/BOOT/icons'))
if misplaced_pngs:
    raise SystemExit(f'icons must ship only in icons/: {misplaced_pngs}')
if len({Path(name).name for name in graphics}) != len(graphics):
    raise SystemExit('duplicate graphics filenames in the Boot bundle')
# All three provenance manifests are useful to CI but never to the firmware.
provenance = sorted(name for name in relative if name.endswith('_PROVENANCE.txt'))
if provenance:
    raise SystemExit(f'build metadata leaked into runtime bundle: {provenance}')
missing = required - relative
if missing:
    raise SystemExit(f'bundle is missing required files: {sorted(missing)}')

# Boot owns one EFI application. Filesystem driver packages are independent.
expected_efi = {'EFI/BOOT/BOOTX64.EFI'}
if {name for name in relative if name.lower().endswith('.efi')} != expected_efi:
    raise SystemExit('Boot bundle contains an independently owned EFI driver')
if any('/drivers_' in name or '/drivers-disabled/' in name for name in relative):
    raise SystemExit('filesystem driver payload leaked into Boot bundle')

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
    if {name for name in names if name.lower().endswith('.efi')} != expected_efi:
        raise SystemExit('written Boot bundle contains an EFI driver')
    bad = archive.testzip()
    if bad is not None:
        raise SystemExit(f'ZIP integrity failure at {bad}')

print(f'{output}: {len(files)} files')
PY
