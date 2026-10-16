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
make -C "$root/filesystems" infiltratorfs_gnuefi "${options[@]}"
test -s "$root/refind/refind_x64.efi"
test -s "$root/filesystems/infiltratorfs_x64.efi"
file "$root/refind/refind_x64.efi" "$root/filesystems/infiltratorfs_x64.efi"
unresolved="$(nm -u "$root/filesystems/infiltratorfs_x64.so")"
if grep -Eq ' (fopen|open|read|write|lseek|mmap|pthread_[[:alnum:]_]*|dlopen|clock_gettime|gettimeofday|getenv|_Exit|fprintf|fputc|ferror|snprintf|__ctype_b_loc)$' <<< "$unresolved"; then
    echo "Host runtime dependency in the read-only EFI driver" >&2
    exit 1
fi
