#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Independent media fixtures for the generic BOOT loader. Formatters here
# are test oracles only; neither BOOT nor its installer invokes them.
set -euo pipefail
export LC_ALL=C
reader=$(realpath "$1")
module=$(realpath "$2")
name="$3"
work=$(mktemp -d)
cleanup(){
    if mountpoint -q "$work/mnt"; then sudo umount "$work/mnt" || return 1; fi
    rm -rf -- "$work"
}
trap cleanup EXIT
cd "$work"
mkdir tree mnt
dd if=/dev/urandom of=tree/proof.bin bs=4093 count=19 status=none
# Every supported fixture is populated by a tool independent of the
# provider, tested at the available sector sizes and rehashed after use.
# Only the independent fixture writer mounts a loopback image, never BOOT.
populate(){
    sudo mount -t "$1" -o loop,rw media.img mnt
    sudo cp tree/proof.bin mnt/proof.bin
    sync
    sudo umount mnt
}
case "$name" in
  ext2|ext3)
    truncate -s 64M media.img
    features=ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file
    if [[ "$name" == ext3 ]]; then features+=,has_journal; fi
    mkfs."$name" -F -q -b 4096 -O "none,$features" -d tree media.img
    sectors=(512 4096)
    ;;
  btrfs)
    truncate -s 192M media.img
    mkfs.btrfs -q -f -s 4096 -n 16384 -m dup -d single --csum crc32c --rootdir tree media.img
    btrfs check --readonly media.img
    sectors=(512 4096)
    ;;
  xfs)
    truncate -s 320M media.img
    printf 'boot\n0 0\nd--755 0 0\nproof.bin ---644 0 0 %s/tree/proof.bin\n$\n' "$work" > proto
    mkfs.xfs -q -f -m rmapbt=1 -p proto media.img
    xfs_repair -n -f media.img
    sectors=(512 4096)
    ;;
  fat12|fat16|fat32)
    case "$name" in
      fat12) truncate -s 1440K media.img; bits=12;;
      fat16) truncate -s 32M media.img; bits=16;;
      fat32) truncate -s 128M media.img; bits=32;;
    esac
    mkfs.fat -F "$bits" media.img
    mcopy -i media.img tree/proof.bin ::/proof.bin
    sectors=(512)
    ;;
  hfs)
    export HOME="$work"
    truncate -s 64M media.img
    hformat -l BOOTV2 media.img
    hcopy -r tree/proof.bin :proof.bin
    sectors=(512)
    ;;
  exfat)
    truncate -s 64M media.img
    mkfs.exfat -L BOOTV2 media.img
    populate exfat
    sectors=(512)
    ;;
  minix)
    truncate -s 64M media.img
    mkfs.minix -3 media.img
    populate minix
    sectors=(512)
    ;;
  hfsplus)
    truncate -s 128M media.img
    mkfs.hfsplus -v BOOTV2 media.img
    populate hfsplus
    sectors=(512)
    ;;
  bfs)
    truncate -s 16M media.img
    mkfs.bfs media.img
    populate bfs
    sectors=(512)
    ;;
  *) echo "No independent media fixture yet for $name" >&2; exit 2;;
esac
sha256sum media.img > media.sha
for sector in "${sectors[@]}"; do
    "$reader" "$module" "$name" media.img tree/proof.bin /proof.bin "$sector"
    sha256sum -c media.sha
done
