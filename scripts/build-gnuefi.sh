#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test "$(uname -m)" = x86_64
test -f "$root/Make.common"
files="$(dpkg -L gnu-efi)"
crt="$(grep '/crt0-efi-x86_64\.o$' <<< "$files" | head -n1)"
efi="$(grep '/libefi\.a$' <<< "$files" | head -n1)"
gnu="$(grep '/libgnuefi\.a$' <<< "$files" | head -n1)"
test -f "$crt" && test -f "$efi" && test -f "$gnu"
options=(ARCH=x86_64 EFIINC=/usr/include/efi
         "EFICRT0=$(dirname "$crt")" "EFILIB=$(dirname "$efi")" "GNUEFILIB=$(dirname "$gnu")")

# Build/link only. Installation, ESP writes and NVRAM mutation are separate.
make -C "$root" gnuefi "${options[@]}"

# Filesystem-driver builds share object names even though their compile flags are
# not identical. Isolate the ext4 and InfiltratorFS builds so one driver's object
# cache cannot silently contaminate the other.
fswork="$(mktemp -d)"
trap 'rm -rf "$fswork"' EXIT
reset_fsw_objects() {
    rm -f "$root/filesystems"/*.o \
          "$root/filesystems"/*.so \
          "$root/filesystems"/*.efi
}

reset_fsw_objects
make -C "$root/filesystems" ext4_gnuefi "${options[@]}"
cp "$root/filesystems/ext4_x64.efi" "$fswork/ext4_x64.efi"
cp "$root/filesystems/ext4_x64.so" "$fswork/ext4_x64.so"

reset_fsw_objects
make -C "$root/filesystems" infiltratorfs_gnuefi "${options[@]}"

# Restore the independently built ext4 artifacts beside the InfiltratorFS
# artifacts for release verification and packaging. The build target has already
# copied ext4_x64.efi into drivers_x64 as Boot's active read-only Linux-root reader.
cp "$fswork/ext4_x64.efi" "$root/filesystems/ext4_x64.efi"
cp "$fswork/ext4_x64.so" "$root/filesystems/ext4_x64.so"

test -s "$root/refind/refind_x64.efi"
test -s "$root/filesystems/ext4_x64.efi"
test -s "$root/filesystems/infiltratorfs_x64.efi"
test -s "$root/drivers_x64/ext4_x64.efi"
file "$root/refind/refind_x64.efi" \
     "$root/filesystems/ext4_x64.efi" \
     "$root/filesystems/infiltratorfs_x64.efi"

for shared in "$root/filesystems/ext4_x64.so" "$root/filesystems/infiltratorfs_x64.so"; do
    unresolved="$(nm -u "$shared")"
    if grep -Eq ' (fopen|open|read|write|lseek|mmap|pthread_[[:alnum:]_]*|dlopen|clock_gettime|gettimeofday|getenv|_Exit|fprintf|fputc|ferror|snprintf|__ctype_b_loc)$' <<< "$unresolved"; then
        echo "Host runtime dependency in EFI filesystem driver: $shared" >&2
        exit 1
    fi
done

# Do not rely on `file`'s prose alone. Verify the PE/COFF machine and subsystem
# contracts Boot and firmware actually consume.
python3 - \
    "$root/refind/refind_x64.efi" \
    "$root/filesystems/ext4_x64.efi" \
    "$root/filesystems/infiltratorfs_x64.efi" <<'PY'
from pathlib import Path
import struct
import sys

expected = {
    Path(sys.argv[1]): 10,  # EFI application
    Path(sys.argv[2]): 11,  # EFI boot-service driver
    Path(sys.argv[3]): 11,
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
