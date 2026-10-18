#!/usr/bin/env bash
# Build an APT-installable Boot payload from the already verified UEFI bundle.
# Neither this script nor the package's installation modifies the EFI partition.
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
bundle="${1:?UEFI bundle path required}"
output="${2:?DEB destination required}"
version="$(tr -d '\r\n' < "$root/VERSION")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
[[ "$output" == *"/infiltrator-boot_${version}_amd64.deb" ]]
test -s "$bundle"
test ! -e "$output"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
pkg="$work/pkg"
mkdir -p "$pkg/DEBIAN" "$pkg/usr/lib/infiltrator-boot/EFI/BOOT/drivers_x64" "$pkg/usr/sbin"
python3 - "$bundle" "$pkg/usr/lib/infiltrator-boot" <<'PY'
from pathlib import Path
import sys, zipfile
bundle, root = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(bundle) as z:
    items = [f for f in z.infolist() if not f.is_dir()]
    names = {f.filename for f in items}
    # The public package must never inherit source-theme/font/icon build records.
    leaked = sorted(name for name in names if name.endswith('_PROVENANCE.txt'))
    if leaked:
        raise SystemExit(f'provenance metadata must not ship in a DEB: {leaked}')
    assert 'EFI/BOOT/BOOTX64.EFI' in names
    assert 'EFI/BOOT/boot.conf' in names
    root_graphics = {'mb-corpo-s-regular.png', 'mb-corpo-s-bold.png',
                     'mb-corpo-a-cond-regular.png', 'boot-banner.png',
                     'boot-selection-big.png', 'boot-selection-small.png'}
    graphics = {name for name in names if name.lower().endswith('.png')}
    misplaced_pngs = sorted(name for name in graphics
                            if Path(name).parent != Path('EFI/BOOT/icons')
                            and name not in {'EFI/BOOT/' + asset for asset in root_graphics})
    if misplaced_pngs:
        raise SystemExit(f'icons must ship only in icons/: {misplaced_pngs}')
    if len({Path(name).name for name in graphics}) != len(graphics):
        raise SystemExit('duplicate graphics filenames in the Boot DEB')
    assert {name for name in names if name.lower().endswith('.efi')} == {'EFI/BOOT/BOOTX64.EFI'}
    forbidden = [name for name in names if '/drivers_' in name or '/drivers-disabled/' in name]
    if forbidden:
        raise SystemExit(f'independently owned EFI driver shipped in Boot DEB: {forbidden}')
    for item in items:
        p = Path(item.filename)
        if (p.is_absolute() or '..' in p.parts or
            len(p.parts) < 3 or p.parts[:2] != ('EFI', 'BOOT') or
            (item.external_attr >> 16) & 0o170000 == 0o120000):
            raise SystemExit(f'unsafe bundle member: {item.filename}')
        target = root / p
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.read(item))
        target.chmod(0o644)
PY
install -m 0755 "$root/packaging/infiltrator-boot-deploy" "$pkg/usr/sbin/infiltrator-boot-deploy"
printf '%s\n' "$version" > "$pkg/usr/lib/infiltrator-boot/VERSION"
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: infiltrator-boot
Version: $version
Section: admin
Priority: optional
Architecture: amd64
Maintainer: Infiltrator Projects
Depends: bash, coreutils, util-linux
Description: InfiltratorOS UEFI boot manager and explicitly deployed firmware payload
 This package stages the verified EFI boot manager and support files.
 To update an existing /boot/efi/EFI/InfiltratorBoot installation, run
 infiltrator-boot-deploy --update-existing --confirm as root.
 Installing the Debian package itself never modifies the ESP or firmware.
CONTROL
mkdir -p "$(dirname "$output")"
dpkg-deb --build --root-owner-group "$pkg" "$output"
test -s "$output"
test "$(dpkg-deb -f "$output" Package)" = infiltrator-boot
test "$(dpkg-deb -f "$output" Version)" = "$version"
test "$(dpkg-deb -f "$output" Architecture)" = amd64
check="$work/check"
mkdir -p "$check"
dpkg-deb -x "$output" "$check"
cmp "$pkg/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI" "$check/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI"
echo "Built independently installable $output with no ESP mutation."
