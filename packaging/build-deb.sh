#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
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
from pathlib import Path, PurePosixPath
import stat
import sys
import zipfile

bundle, root = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(bundle) as z:
    items = [item for item in z.infolist() if not item.is_dir()]
    filenames = [item.filename for item in items]
    names = set(filenames)
    # A set-only inventory hides duplicate ZIP entries, allowing an unchecked
    # second member to overwrite the executable or a verified resource.
    if len(names) != len(filenames):
        raise SystemExit('duplicate bundle member names are forbidden')
    required = {'EFI/BOOT/BOOTX64.EFI', 'EFI/BOOT/boot.conf'}
    if not required <= names:
        raise SystemExit(f'missing Boot bundle files: {sorted(required - names)}')
    leaked = sorted(name for name in names if name.endswith('_PROVENANCE.txt'))
    if leaked:
        raise SystemExit(f'provenance metadata must not ship in a DEB: {leaked}')
    root_graphics = {'mb-corpo-s-regular.png', 'mb-corpo-s-bold.png',
                     'mb-corpo-a-cond-regular.png', 'boot-banner.png',
                     'boot-selection-big.png', 'boot-selection-small.png'}
    graphics = {name for name in names if name.lower().endswith('.png')}
    misplaced_pngs = sorted(name for name in graphics
                            if PurePosixPath(name).parent != PurePosixPath('EFI/BOOT/icons')
                            and name not in {'EFI/BOOT/' + asset for asset in root_graphics})
    if misplaced_pngs:
        raise SystemExit(f'icons must ship only in icons/: {misplaced_pngs}')
    if len({PurePosixPath(name).name for name in graphics}) != len(graphics):
        raise SystemExit('duplicate graphics filenames in the Boot DEB')
    if {name for name in names if name.lower().endswith('.efi')} != {'EFI/BOOT/BOOTX64.EFI'}:
        raise SystemExit('Boot must contain exactly its own EFI executable')
    forbidden = [name for name in names if '/drivers_' in name or '/drivers-disabled/' in name]
    if forbidden:
        raise SystemExit(f'independently owned EFI driver shipped in Boot DEB: {forbidden}')
    if sum(item.file_size for item in items) > 256 * 1024 * 1024:
        raise SystemExit('Boot bundle expands beyond the permitted 256 MiB')
    # Validate the complete inventory before writing any bundle member.
    for item in items:
        p = PurePosixPath(item.filename)
        kind = stat.S_IFMT(item.external_attr >> 16)
        if (item.filename.startswith('/') or '\\' in item.filename or
            '..' in p.parts or len(p.parts) < 3 or p.parts[:2] != ('EFI', 'BOOT') or
            kind not in (0, stat.S_IFREG) or item.file_size > 64 * 1024 * 1024):
            raise SystemExit(f'unsafe bundle member: {item.filename}')
    for item in items:
        target = root.joinpath(*PurePosixPath(item.filename).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.read(item))
        target.chmod(0o644)
PY
install -m 0755 "$root/packaging/infiltrator-boot-deploy" "$pkg/usr/sbin/infiltrator-boot-deploy"
install -m 0644 "$root/packaging/infiltrator-boot-deploy-core" "$pkg/usr/lib/infiltrator-boot/deploy-core"
printf '%s\n' "$version" > "$pkg/usr/lib/infiltrator-boot/VERSION"
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: infiltrator-boot
Version: $version
Section: admin
Priority: optional
Architecture: amd64
Maintainer: Infiltrator Projects
Depends: bash, coreutils, findutils, util-linux
Description: InfiltratorOS UEFI boot manager and explicitly deployed firmware payload
 This package stages the verified EFI boot manager and support files.
 To provision a fresh /boot/efi/EFI/InfiltratorBoot installation, run
 infiltrator-boot-deploy --install-new --confirm as root.
 To update an existing installation and all managed graphics, run
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
cmp "$root/packaging/infiltrator-boot-deploy-core" "$check/usr/lib/infiltrator-boot/deploy-core"
cmp "$root/packaging/infiltrator-boot-deploy" "$check/usr/sbin/infiltrator-boot-deploy"
echo "Built independently installable $output with no ESP mutation."
