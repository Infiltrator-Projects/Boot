#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# This OVMF test operates ONLY on two disposable disk image files.
set -euo pipefail
export LC_ALL=C
test -s boot/boot_x64.efi
for tool in qemu-system-x86_64 mkfs.fat mcopy mmd sgdisk dd timeout; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
ovmf=/usr/share/OVMF
test -s "$ovmf/OVMF_CODE_4M.fd"
test -s "$ovmf/OVMF_VARS_4M.fd"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
esp="$work/esp.img"
disk="$work/disk.img"
truncate -s 64M "$esp"
mkfs.fat -F32 "$esp"
mmd -i "$esp" ::/EFI ::/EFI/BOOT
# Fallback path here belongs exclusively to QEMU's throwaway disk image.
mcopy -i "$esp" boot/boot_x64.efi ::/EFI/BOOT/BOOTX64.EFI
cat > "$work/boot.conf" <<'CONFIG'
timeout 3
log_level 4
use_nvram false
textonly true
scanfor internal,external
showtools about,reboot
CONFIG
mcopy -i "$esp" "$work/boot.conf" ::/EFI/BOOT/boot.conf
truncate -s 80M "$disk"
sgdisk --clear --new=1:2048:+64M --typecode=1:ef00 "$disk"
dd if="$esp" of="$disk" bs=1M seek=1 conv=notrunc status=none
cp "$ovmf/OVMF_VARS_4M.fd" "$work/vars.fd"
qemu=(
 qemu-system-x86_64 -machine q35,accel=tcg -cpu max
 -m 768 -display none -monitor none -net none -no-reboot
 -serial "file:$work/serial.log"
 -drive "if=pflash,format=raw,readonly=on,file=$ovmf/OVMF_CODE_4M.fd"
 -drive "if=pflash,format=raw,file=$work/vars.fd"
 -drive "file=$disk,format=raw,if=none,id=disk"
 -device nvme,drive=disk,serial=boot-smoke
)
status=0
timeout 40 "${qemu[@]}" || status=$?
if [[ "$status" != 0 && "$status" != 124 ]]; then
 echo "QEMU exited with unexpected status $status" >&2
 tail -n 60 "$work/serial.log" >&2 || true
 exit 1
fi
dd if="$disk" of="$work/after.img" bs=1M skip=1 count=64 status=none
if ! mcopy -i "$work/after.img" ::/EFI/BOOT/BOOT.log "$work/BOOT.log"; then
 echo "BOOT startup log missing from disposable EFI disk" >&2
 tail -n 60 "$work/serial.log" >&2 || true
 exit 1
fi
if ! grep -Fq 'Entering main loop' "$work/BOOT.log"; then
 echo "BOOT did not reach main menu under OVMF" >&2
 tail -n 65 "$work/BOOT.log" >&2 || true
 exit 1
fi
echo "BOOT firmware executable reached main menu in OVMF"
