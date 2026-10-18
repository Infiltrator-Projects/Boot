#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test "$(uname -m)" = x86_64
test -f "$root/Make.common"
gnu_efi_prefix="${GNU_EFI_PREFIX:-/usr}"
if [[ -n ${GNU_EFI_PREFIX:-} ]]; then
    files="$(find "$gnu_efi_prefix/lib" -type f -print)"
else
    files="$(dpkg -L gnu-efi)"
fi
crt="$(grep '/crt0-efi-x86_64\.o$' <<< "$files" | head -n1)"
efi="$(grep '/libefi\.a$' <<< "$files" | head -n1)"
gnu="$(grep '/libgnuefi\.a$' <<< "$files" | head -n1)"
test -f "$crt" && test -f "$efi" && test -f "$gnu"
options=(ARCH=x86_64 "EFIINC=$gnu_efi_prefix/include/efi"
         "EFICRT0=$(dirname "$crt")" "EFILIB=$(dirname "$efi")" "GNUEFILIB=$(dirname "$gnu")")

# Build/link only. Installation, ESP writes and NVRAM mutation are separate.
make -C "$root" gnuefi "${options[@]}"

test -s "$root/refind/refind_x64.efi"
file "$root/refind/refind_x64.efi"

# Do not rely on `file`'s prose alone. Verify the PE/COFF machine and subsystem
# contracts Boot and firmware actually consume.
python3 - \
    "$root/refind/refind_x64.efi" <<'PY'
from pathlib import Path
import struct
import sys

expected = {
    Path(sys.argv[1]): 10,  # EFI application
}
for path, subsystem_expected in expected.items():
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b'MZ':
        raise SystemExit(f'{path}: missing DOS/PE image signature')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if pe + 96 > len(data) or data[pe:pe+4] != b'PE\0\0':
        raise SystemExit(f'{path}: invalid PE header offset/signature')
    machine = struct.unpack_from('<H', data, pe + 4)[0]
    optional_magic = struct.unpack_from('<H', data, pe + 24)[0]
    subsystem = struct.unpack_from('<H', data, pe + 24 + 68)[0]
    if machine != 0x8664:
        raise SystemExit(f'{path}: expected x86-64 machine 0x8664, got 0x{machine:04x}')
    if optional_magic != 0x20B:
        raise SystemExit(f'{path}: expected PE32+ optional header, got 0x{optional_magic:04x}')
    if subsystem != subsystem_expected:
        raise SystemExit(f'{path}: expected EFI subsystem {subsystem_expected}, got {subsystem}')
    print(f'{path}: PE32+ x86-64 EFI subsystem {subsystem} OK')
PY
