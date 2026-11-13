#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
module=${1:?pass the canonical FAT16.so}
module=$(realpath "$module")
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
cxx=${CXX:-c++}
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread \
    "$root/tests/so_fixture_test.cpp" -o "$work/so-fixtures"
"$work/so-fixtures" malformed "$work/module-test" "$module"
truncate -s 32M "$work/media.img"
mkfs.fat -F 16 -I -n SOFIXTURE "$work/media.img"
"$work/so-fixtures" generate-data "$work/data.bin"
mmd -i "$work/media.img" '::/Nested Directory'
mcopy -i "$work/media.img" "$work/data.bin" '::/Nested Directory/Boot Read Test.bin'
mcopy -i "$work/media.img" "$work/data.bin" '::/Nested Directory/Café Ü.bin'
before=$(sha256sum "$work/media.img")
"$work/so-fixtures" whole-disk "$work/media.img" "$work/whole-disk.img"
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
