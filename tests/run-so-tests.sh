#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
module=${1:?pass the canonical FAT16.so}
source_root=${2:?pass the matching Filesystem-FAT16 checkout}
module=$(realpath "$module")
cmp "$root/include/ifs_boot_module.h" "$source_root/native/include/infiltratr/fs/boot_module.h"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cc=${CC:-cc}
sanitizers=()
if [[ ${IFS_SO_SANITIZE:-0} == 1 ]]; then
    sanitizers=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"$cc" -std=c11 -O2 -Wall -Wextra -Werror \
    "${sanitizers[@]}" \
    "$root/refind/so_elf.c" "$root/tests/so_module_test.c" -o "$work/module-test"
python3 "$root/tests/test_so_malformed.py" "$work/module-test" "$module"
truncate -s 32M "$work/media.img"
mkfs.fat -F 16 -I -n SOFIXTURE "$work/media.img"
python3 - "$work/data.bin" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes((i*37+3)%256 for i in range(137123)))
PY
mmd -i "$work/media.img" '::/Nested Directory'
mcopy -i "$work/media.img" "$work/data.bin" '::/Nested Directory/Boot Read Test.bin'
mcopy -i "$work/media.img" "$work/data.bin" '::/Nested Directory/Café Ü.bin'
before=$(sha256sum "$work/media.img")
python3 - "$work/media.img" "$work/whole-disk.img" <<'PY'
from pathlib import Path
import struct
import sys
source = Path(sys.argv[1]).read_bytes()
mbr = bytearray(512)
mbr[446+4] = 6
struct.pack_into('<II', mbr, 446+8, 2048, len(source)//512)
mbr[510:] = b'\x55\xaa'
with Path(sys.argv[2]).open('wb') as output:
    output.write(mbr)
    output.seek(1024*1024)
    output.write(source)
PY
"$work/module-test" "$module" "$work/media.img" '/Nested Directory/Boot Read Test.bin' "$work/data.bin"
prefix=${GNU_EFI_PREFIX:-/usr}
"$cc" -std=c11 -O2 -fshort-wchar -ffunction-sections -fdata-sections \
    "${sanitizers[@]}" \
    -Wall -Wextra -Werror -D__MAKEWITH_GNUEFI -DEFIX64 \
    -I"$prefix/include/efi" -I"$prefix/include/efi/x86_64" \
    -I"$prefix/include/efi/protocol" \
    "$root/refind/so_elf.c" "$root/tests/so_uefi_test.c" \
    -Wl,--gc-sections "$prefix/lib/libefi.a" -o "$work/uefi-test"
"$work/uefi-test" "$module" "$work/media.img" "$work/whole-disk.img"
test "$(sha256sum "$work/media.img")" = "$before"
