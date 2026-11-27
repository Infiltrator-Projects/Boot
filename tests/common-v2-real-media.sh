#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
common=${COMMON_INCLUDE_DIR:?canonical Common include directory}
ext4=${EXT4_FIRMWARE:?real EXT4 firmware module}
ntfs=${NTFS_FIRMWARE:?real NTFS firmware module}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I"$common" -O2 -fno-tree-loop-distribute-patterns \
    tests/common_v2_media_test.c boot/common_v2_elf.c boot/common_v2_module.c \
    boot/common_v2_storage.c boot/common_v2_trust.c -o "$work/test"
mkdir -p "$work/tree"
# Nontrivial bytes and a non-sector-aligned file length exercise partial
# reads across actual filesystem block and host sector boundaries.
dd if=/dev/urandom of="$work/tree/proof.bin" bs=4093 count=19 status=none
features=has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,flex_bg
features+=,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum
for sector in 512 4096; do
    extimg="$work/ext-$sector.img"
    truncate -s 64M "$extimg"
    mkfs.ext4 -q -F -b 4096 -O "none,$features" -L EXT4V2 -d "$work/tree" "$extimg"
    sha256sum "$extimg" > "$work/before.sha"
    "$work/test" "$ext4" ext4 "$extimg" "$work/tree/proof.bin" /proof.bin "$sector"
    sha256sum -c "$work/before.sha"
    ntimg="$work/ntfs-$sector.img"
    truncate -s 64M "$ntimg"
    mkntfs -F -Q -s "$sector" -c 4096 -L NTFSV2 "$ntimg"
    ntfscp -f "$ntimg" "$work/tree/proof.bin" /proof.bin
    sha256sum "$ntimg" > "$work/before.sha"
    "$work/test" "$ntfs" ntfs "$ntimg" "$work/tree/proof.bin" /proof.bin "$sector"
    sha256sum -c "$work/before.sha"
done
