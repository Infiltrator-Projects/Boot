#!/usr/bin/env bash
# Boots the compiled EFI application in QEMU/OVMF using a disposable FAT32
# GPT disk and isolated firmware variable image. BOOT must start a gzipped
# copy of its Btrfs EFI driver and reach the main menu.

set -euo pipefail
export LC_ALL=C
test -s boot/boot_x64.efi
for tool in qemu-system-x86_64 mkfs.fat mcopy mmd sgdisk dd timeout gzip; do
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
mmd -i "$esp" ::/EFI ::/EFI/BOOT ::/EFI/BOOT/drivers_x64

mcopy -i "$esp" boot/boot_x64.efi ::/EFI/BOOT/BOOTX64.EFI
driver=drivers_x64/btrfs_x64.efi
driver_size=$(stat -c %s "$driver")
gzip -nc "$driver" > "$work/driver.gz"
mcopy -i "$esp" "$work/driver.gz" ::/EFI/BOOT/drivers_x64/btrfs_x64.efi
cat > "$work/boot.conf" <<'CONFIG'
timeout 3
log_level 4
use_nvram false
textonly true
support_gzipped_loaders true
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
# An interactive menu can legitimately outlive the timeout. The exit code
# alone cannot distinguish that from a stalled boot. The test reads the
# post-run disk image and requires the UTF-16 log marker from the menu loop.
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

# A decoded-byte-count match and driver StartImage log establish that BOOT
# launched the compressed EFI driver, not that the driver mounted a Btrfs
# volume or that any Linux/Windows OS reached userspace.
iconv -f UTF-16LE -t UTF-8 "$work/BOOT.log" > "$work/log.txt"
if ! grep -Fq "gunzip status 0, decoded $driver_size bytes" "$work/log.txt" ||
   ! grep -Fiq "Launching 'btrfs_x64.efi'" "$work/log.txt" ||
   ! grep -Fq 'Program has returned 0' "$work/log.txt"; then
 echo "BOOT did not decode and start the complete gzip EFI driver" >&2
 grep -Ei 'gzip|btrfs|launching|returned' "$work/log.txt" >&2 || true
 exit 1
fi
if ! grep -Fq 'Entering main loop' "$work/log.txt"; then
 echo "BOOT did not reach main menu under OVMF" >&2
 tail -n 65 "$work/log.txt" >&2 || true
 exit 1
fi
echo "BOOT started the $driver_size-byte Btrfs driver from gzip and reached the main menu in OVMF"
