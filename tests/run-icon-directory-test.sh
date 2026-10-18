#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
efi_prefix="${GNU_EFI_PREFIX:-/usr}"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
efi_lib="$(find "$efi_prefix/lib" -name libefi.a -type f -print -quit)"
test -s "$efi_lib"

"${CC:-cc}" -O1 -fshort-wchar -ffunction-sections -fdata-sections \
    -D__MAKEWITH_GNUEFI -DEFIX64 -DEFI_FUNCTION_WRAPPER \
    -I"$efi_prefix/include/efi" -I"$efi_prefix/include/efi/x86_64" \
    -I"$efi_prefix/include/efi/protocol" -I"$root/include" \
    -I"$root/refind" -I"$root/libeg" -I"$root/mok" \
    "$root/tests/icon_directory_test.c" "$root/libeg/image.c" \
    "$root/refind/mystrings.c" "$root/libeg/lodepng.c" \
    "$root/libeg/lodepng_xtra.c" "$root/libeg/load_bmp.c" \
    "$root/libeg/load_icns.c" "$root/libeg/nanojpeg.c" \
    "$root/libeg/nanojpeg_xtra.c" \
    -Wl,--gc-sections "$efi_lib" -o "$work/icon-directory-test"
"$work/icon-directory-test" "$root/icons/os_linux.png"
