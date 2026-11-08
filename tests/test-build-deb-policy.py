#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adversarial Boot package checks: reject malformed archives before extraction."""
from __future__ import annotations

from pathlib import Path
import stat
import subprocess
import tempfile
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text().strip()
BUILD = ROOT / "packaging/build-deb.sh"

MINIMAL = {
    "EFI/BOOT/BOOTX64.EFI": b"MZ-boot-qualification",
    "EFI/BOOT/boot.conf": b"timeout 1\n",
    "EFI/BOOT/icons/os_infiltratoros.png": b"PNG-qualification",
}


def verify_case(folder: Path, label: str, entries: list[tuple[str, bytes, int]],
                should_pass: bool) -> None:
    archive = folder / f"{label}.zip"
    output = folder / label / f"infiltrator-boot_{VERSION}_amd64.deb"
    with zipfile.ZipFile(archive, "w") as bundle:
        for name, body, mode in entries:
            item = zipfile.ZipInfo(name)
            item.create_system = 3
            item.external_attr = mode << 16
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                bundle.writestr(item, body)
    result = subprocess.run(["bash", str(BUILD), str(archive), str(output)],
                            capture_output=True, text=True, check=False)
    if (result.returncode == 0) != should_pass:
        raise AssertionError(f"{label}: unexpected result {result.returncode}: "
                             f"{result.stdout}\n{result.stderr}")
    if should_pass and not output.is_file():
        raise AssertionError(f"{label}: package not produced")
    if not should_pass and output.exists():
        raise AssertionError(f"{label}: rejected archive produced a package")


def main() -> None:
    regular = stat.S_IFREG | 0o644
    entries = [(name, data, regular) for name, data in MINIMAL.items()]
    with tempfile.TemporaryDirectory(prefix="infiltrator-boot-policy-") as directory:
        folder = Path(directory)
        verify_case(folder, "valid", entries, True)
        verify_case(folder, "duplicate", entries + [entries[0]], False)
        verify_case(folder, "missing-efi", entries[1:], False)
        verify_case(folder, "foreign-efi",
                    entries + [("EFI/BOOT/drivers_x64/foreign.efi", b"MZ", regular)], False)
        verify_case(folder, "traversal",
                    entries + [("EFI/BOOT/../bad.txt", b"not-allowed", regular)], False)
        verify_case(folder, "symlink",
                    entries + [("EFI/BOOT/icons/alias.png", b"/etc/passwd",
                                stat.S_IFLNK | 0o777)], False)
        verify_case(folder, "absolute",
                    entries + [("/EFI/BOOT/not-relative.txt", b"bad", regular)], False)
    print("BOOT_BUNDLE_POLICY_PASS: valid package and six adversarial cases")


if __name__ == "__main__":
    main()
