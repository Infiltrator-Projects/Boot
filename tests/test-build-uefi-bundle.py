#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep Boot's independently-owned UEFI bundle deterministic and self-contained."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/build-uefi-bundle.sh"
VERSION = (ROOT / "VERSION").read_text().strip()

ROOT_GRAPHICS = (
    "mb-corpo-s-regular.png", "mb-corpo-s-bold.png",
    "mb-corpo-a-cond-regular.png", "boot-banner.png",
    "boot-selection-big.png", "boot-selection-small.png",
)
ICON_GRAPHICS = (
    "os_infiltratoros.png", "os_infiltrator.png", "os_infiltratorboot.png",
    "func_reset.png", "func_shutdown.png", "func_firmware.png",
    "os_uefi.png", "os_unknown.png",
)


def execute(dist: Path, icons: Path, output: Path, success: bool) -> None:
    result = subprocess.run(
        ["bash", str(SCRIPT), str(dist), str(icons), VERSION, str(output)],
        capture_output=True, text=True, timeout=30,
    )
    if success:
        assert result.returncode == 0, result.stderr
        assert output.is_file(), "Bundle was not written"
    else:
        assert result.returncode != 0, "Invalid bundle passed validation"


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="boot-bundle-regression-") as temp:
        root = Path(temp)
        dist, icons = root / "dist", root / "icons"
        dist.mkdir()
        icons.mkdir()
        (dist / f"Boot-{VERSION}-x86_64.efi").write_bytes(b"efi-test")
        (dist / "boot.conf").write_text("timeout 1\n")
        for name in ROOT_GRAPHICS:
            (dist / name).write_bytes(("root:" + name).encode())
        for name in ICON_GRAPHICS:
            (icons / name).write_bytes(("icon:" + name).encode())
        (icons / "README").write_text("attribution fixture\n")
        licences = icons / "licenses"
        licences.mkdir()
        (licences / "COPYING").write_text("test licence text\n")

        first, second = root / "first.zip", root / "second.zip"
        execute(dist, icons, first, success=True)
        execute(dist, icons, second, success=True)
        assert hashlib.sha256(first.read_bytes()).digest() == hashlib.sha256(
            second.read_bytes()).digest(), "Identical inputs produced different bundles"
        with zipfile.ZipFile(first) as archive:
            names = archive.namelist()
            assert names == sorted(names), "ZIP order is not deterministic"
            assert names.count("EFI/BOOT/BOOTX64.EFI") == 1
            assert "EFI/BOOT/icons/licenses/COPYING" in names
            assert not any(name.endswith(".so") or name.endswith("_x64.efi")
                           for name in names), "Foreign driver leaked into Boot"

        (icons / "os_unknown.png").unlink()
        execute(dist, icons, root / "missing.zip", success=False)
        (icons / "os_unknown.png").write_bytes(b"restored")

        # A licence subdirectory is copied recursively; a malicious or
        # accidental link must never cause host files to enter the ZIP.
        target = root / "outside.txt"
        target.write_text("do not package this\n")
        (licences / "external").symlink_to(target)
        result = subprocess.run(
            ["bash", str(SCRIPT), str(dist), str(icons), VERSION,
             str(root / "linked.zip")],
            capture_output=True, text=True, timeout=30,
        )
        assert result.returncode != 0, "Symlink unexpectedly accepted"
        assert "symbolic links are not permitted" in result.stderr, result.stderr

    print("BOOT_BUNDLE_REGRESSION_PASS: deterministic, fail-closed, no external links")


if __name__ == "__main__":
    main()
