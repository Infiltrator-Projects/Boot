#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dist="${1:?dist directory required}"
icons_dir="${2:?generated icons directory required}"
version="${3:-$(tr -d '\r\n' < "$root/VERSION")}"
output="${4:-$dist/Boot-$version-x86_64-uefi.zip}"

boot_efi="$dist/Boot-$version-x86_64.efi"
driver_manifest="$root/filesystems/boot-driver-manifest.tsv"
test -s "$driver_manifest"
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
mkdir -p "$boot_dir/drivers_x64" "$boot_dir/drivers-disabled" "$boot_dir/icons"

install -m 0644 "$boot_efi" "$boot_dir/BOOTX64.EFI"

# Enable only explicitly qualified filesystem drivers. The manifest is the
# single staging authority; adding AmigaFS or further modules does not require
# modifying Boot's loader. Disabled drivers are shipped separately and cannot
# auto-load. Dist artifact names are derived from the same public version.
python3 - "$driver_manifest" "$dist" "$boot_dir" "$version" <<'PY_DRIVERS'
from pathlib import Path
import re
import shutil
import sys

manifest = Path(sys.argv[1])
dist = Path(sys.argv[2])
boot_dir = Path(sys.argv[3])
version = sys.argv[4]
loaded = {}
enabled = 0
for lineno, raw in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
    entry = raw.strip()
    if not entry or entry.startswith("#"):
        continue
    parts = entry.split()
    if len(parts) != 3:
        raise SystemExit(f"bad driver manifest line {lineno}: expected state, EFI filename, artifact prefix")
    state, dest_name, prefix = parts
    if state not in {"enabled", "disabled"}:
        raise SystemExit(f"invalid driver state {state!r} on line {lineno}")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*_x64[.]efi", dest_name):
        raise SystemExit(f"invalid driver filename on line {lineno}")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*", prefix):
        raise SystemExit(f"invalid artifact prefix on line {lineno}")
    if dest_name in loaded:
        raise SystemExit(f"duplicate driver {dest_name}")
    loaded[dest_name] = state
    source = dist / f"{prefix}-{version}-x86_64.efi"
    if not source.is_file() or source.stat().st_size == 0:
        raise SystemExit(f"missing or empty qualified driver: {source}")
    dest_dir = "drivers_x64" if state == "enabled" else "drivers-disabled"
    shutil.copyfile(source, boot_dir / dest_dir / dest_name)
    if state == "enabled":
        enabled += 1
if not enabled:
    raise SystemExit("no enabled filesystem drivers")
print(f"Staged {enabled} enabled and {len(loaded) - enabled} disabled EFI drivers")
PY_DRIVERS

install -m 0644 "$dist/boot.conf" "$boot_dir/boot.conf"
install -m 0644 "$dist/mb-corpo-s-regular.png" "$boot_dir/mb-corpo-s-regular.png"
install -m 0644 "$dist/mb-corpo-s-bold.png" "$boot_dir/mb-corpo-s-bold.png"
install -m 0644 "$dist/mb-corpo-a-cond-regular.png" "$boot_dir/mb-corpo-a-cond-regular.png"
install -m 0644 "$dist/boot-banner.png" "$boot_dir/boot-banner.png"
install -m 0644 "$dist/boot-selection-big.png" "$boot_dir/boot-selection-big.png"
install -m 0644 "$dist/boot-selection-small.png" "$boot_dir/boot-selection-small.png"

# Physical Dell firmware successfully loaded direct PNG assets beside
# BOOTX64.EFI but inherited icons/<name> lookups fell back to dummy graphics.
# Keep the runtime lookup deterministic and retain icons/ as provenance/licence
# material for the shipped artwork.
find "$icons_dir" -maxdepth 1 -type f -name '*.png' -print0 |
while IFS= read -r -d '' icon; do
    install -m 0644 "$icon" "$boot_dir/$(basename "$icon")"
done
cp -R "$icons_dir/." "$boot_dir/icons/"
# Provenance manifests belong to the build evidence, not the EFI runtime.
rm -f -- "$boot_dir/icons/ICON_PROVENANCE.txt"

# Write the ZIP deterministically: sorted paths, fixed timestamps and stable
# permissions. This makes the ready-to-copy UEFI bundle an exact release input.
python3 - "$stage" "$output" "$driver_manifest" <<'PY'
from pathlib import Path
import sys
import zipfile

stage = Path(sys.argv[1])
output = Path(sys.argv[2])
manifest = Path(sys.argv[3])
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
    'EFI/BOOT/os_infiltratoros.png',
    'EFI/BOOT/os_infiltrator.png',
    'EFI/BOOT/os_infiltratorboot.png',
    'EFI/BOOT/func_reset.png',
    'EFI/BOOT/func_shutdown.png',
    'EFI/BOOT/func_firmware.png',
    'EFI/BOOT/os_uefi.png',
    'EFI/BOOT/os_unknown.png',
}
files = sorted(path for path in stage.rglob('*') if path.is_file())
relative = {path.relative_to(stage).as_posix() for path in files}
# All three provenance manifests are useful to CI but never to the firmware.
provenance = sorted(name for name in relative if name.endswith('_PROVENANCE.txt'))
if provenance:
    raise SystemExit(f'build metadata leaked into runtime bundle: {provenance}')
missing = required - relative
if missing:
    raise SystemExit(f'bundle is missing required files: {sorted(missing)}')

expected_drivers = set()
for raw in manifest.read_text(encoding="utf-8").splitlines():
    entry = raw.strip()
    if not entry or entry.startswith("#"):
        continue
    state, dest_name, prefix = entry.split()
    target = 'drivers_x64' if state == 'enabled' else 'drivers-disabled'
    expected_drivers.add(f'EFI/BOOT/{target}/{dest_name}')
actual_drivers = {name for name in relative
                  if name.startswith('EFI/BOOT/drivers_x64/') or
                     name.startswith('EFI/BOOT/drivers-disabled/')}
if actual_drivers != expected_drivers:
    raise SystemExit(f'driver manifest/staging mismatch: expected {sorted(expected_drivers)}, got {sorted(actual_drivers)}')
required.update(expected_drivers)

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
    actual_drivers = {name for name in names
                      if name.startswith('EFI/BOOT/drivers_x64/') or
                         name.startswith('EFI/BOOT/drivers-disabled/')}
    if actual_drivers != expected_drivers:
        raise SystemExit('written bundle driver inventory does not match the manifest')
    bad = archive.testzip()
    if bad is not None:
        raise SystemExit(f'ZIP integrity failure at {bad}')

print(f'{output}: {len(files)} files')
PY
