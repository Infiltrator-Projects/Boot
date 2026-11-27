#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Real firmware and real independent filesystem payloads; never modifies the
# host ESP, firmware variables, physical disk, or system signing identity.
set -euo pipefail
export LC_ALL=C
cd "$(dirname "$0")/.."
ext4=${EXT4_FIRMWARE:?real EXT4 firmware module}
ntfs=${NTFS_FIRMWARE:?real NTFS firmware module}
test -s boot/boot_x64.efi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ovmf=${BOOT_V2_OVMF_DIR:-/usr/share/OVMF}
test -s "$ovmf/OVMF_CODE_4M.secboot.fd"
test -s "$ovmf/OVMF_VARS_4M.fd"
build_efi(){
    local name=$1
    gcc -std=gnu11 -Wall -Wextra -Werror -fpic -fshort-wchar -mno-red-zone -fno-stack-protector \
        -DEFI_FUNCTION_WRAPPER -I/usr/include/efi -I/usr/include/efi/x86_64 \
        -I/usr/include/efi/protocol -c "tests/common_v2_ovmf_$name.c" -o "$work/$name.o"
    ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
        /usr/lib/crt0-efi-x86_64.o "$work/$name.o" -L/usr/lib -lefi -lgnuefi -o "$work/$name.so"
    objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rodata \
        -j .rel -j .rela -j .reloc --target=efi-app-x86_64 "$work/$name.so" "$work/$name.efi"
}
build_efi provision
build_efi child
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_esl.c boot/common_v2_trust.c -o "$work/esl"
openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes -days 2 \
    -subj '/CN=Disposable BOOT Common V2 qualification/' -keyout "$work/key.pem" -out "$work/cert.pem"
owner=d822c2b1-26aa-44a4-9b47-409ca837118f
cert-to-efi-sig-list -g "$owner" "$work/cert.pem" "$work/cert.esl"
cp "$work/cert.esl" "$work/db.esl"
"$work/esl" "$ext4" >> "$work/db.esl"
"$work/esl" "$ntfs" >> "$work/db.esl"
printf 'not ELF but explicitly authenticated\n' > "$work/BAD.so"
"$work/esl" "$work/BAD.so" >> "$work/db.esl"
printf 'revoked test image\n' > "$work/REVOKED.so"
"$work/esl" "$work/REVOKED.so" >> "$work/db.esl"
"$work/esl" "$work/REVOKED.so" > "$work/dbx.esl"
for var in db dbx KEK PK; do
    case "$var" in KEK|PK) esl="$work/cert.esl";; *) esl="$work/$var.esl";; esac
    sign-efi-sig-list -k "$work/key.pem" -c "$work/cert.pem" "$var" "$esl" "$work/$var.auth"
done
sign(){
    sbsign --key "$work/key.pem" --cert "$work/cert.pem" --output "$2" "$1"
    sbverify --cert "$work/cert.pem" "$2"
}
sign "$work/provision.efi" "$work/launcher-signed.efi"
sign "$work/child.efi" "$work/v2child.efi"
sign boot/boot_x64.efi "$work/BOOTX64.EFI"
sign drivers_x64/ext4_x64.efi "$work/ext4_x64.efi"
esp="$work/esp.img";disk="$work/disk.img"
truncate -s 64M "$esp";mkfs.fat -F32 "$esp"
mmd -i "$esp" ::/EFI ::/EFI/BOOT ::/EFI/InfiltratorOS ::/EFI/InfiltratorOS/BOOT ::/EFI/InfiltratorOS/BOOT/drivers_x64
mcopy -i "$esp" "$work/launcher-signed.efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$esp" "$work/BOOTX64.EFI" ::/EFI/InfiltratorOS/BOOT/BOOTX64.EFI
for var in db dbx KEK PK; do mcopy -i "$esp" "$work/$var.auth" "::/$var.auth";done
mcopy -i "$esp" "$ext4" ::/EFI/InfiltratorOS/BOOT/drivers_x64/EXT4.so
mcopy -i "$esp" "$ntfs" ::/EFI/InfiltratorOS/BOOT/drivers_x64/NTFS.so
mcopy -i "$esp" "$work/BAD.so" "$work/REVOKED.so" "$work/ext4_x64.efi" ::/EFI/InfiltratorOS/BOOT/drivers_x64/
printf 'not trusted\n' > "$work/UNTRUSTED.so"
mcopy -i "$esp" "$work/UNTRUSTED.so" ::/EFI/InfiltratorOS/BOOT/drivers_x64/
cat > "$work/boot.conf" <<'CONFIG'
timeout 3
log_level 4
startup_diagnostics on
use_nvram false
textonly true
scanfor internal,external
dont_scan_files +,EFI/BOOT/bootx64.efi
showtools about,reboot
default_selection v2child
CONFIG
mcopy -i "$esp" "$work/boot.conf" ::/EFI/InfiltratorOS/BOOT/boot.conf
# Same VM has a real EXT4 volume for the existing EFI driver, and real
# NTFS for the new provider. The module cannot displace the EXT4 EFI SFS.
mkdir "$work/tree"
printf 'existing EFI reader\n' > "$work/tree/efi-proof.txt"
extimg="$work/ext.img";truncate -s 64M "$extimg"
features=has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,flex_bg
features+=,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum
mkfs.ext4 -q -F -b 4096 -O "none,$features" -L EFIEXT4 -d "$work/tree" "$extimg"
ntimg="$work/ntfs.img";truncate -s 64M "$ntimg"
mkntfs -F -Q -s 512 -c 4096 -L NTFSV2 "$ntimg"
printf 'Common V2 native media proof\n' > "$work/proof.txt"
ntfscp -f "$ntimg" "$work/proof.txt" /proof.txt
ntfscp -f "$ntimg" "$work/v2child.efi" /v2child.efi
truncate -s 200M "$disk"
sgdisk --clear --new=1:2048:+64M --typecode=1:ef00 --new=2:133120:+64M \
    --typecode=2:8300 --new=3:264192:+64M --typecode=3:0700 "$disk"
dd if="$esp" of="$disk" bs=1M seek=1 conv=notrunc status=none
dd if="$extimg" of="$disk" bs=1M seek=65 conv=notrunc status=none
dd if="$ntimg" of="$disk" bs=1M seek=129 conv=notrunc status=none
cp "$ovmf/OVMF_VARS_4M.fd" "$work/vars.fd"
qemu=(
 qemu-system-x86_64 -machine q35,smm=on,accel=tcg -cpu max -m 1024
 -global driver=cfi.pflash01,property=secure,value=on
 # Debian's Secure Boot OVMF disables the standard memory-attribute
 # protocol by default; explicitly retain the protocol in this test VM.
 -fw_cfg name=opt/org.tianocore/UninstallMemAttrProtocol,string=no
 -display none -monitor none -net none -no-reboot
 -drive "if=pflash,format=raw,readonly=on,file=$ovmf/OVMF_CODE_4M.secboot.fd"
 -drive "if=pflash,format=raw,file=$work/vars.fd"
 -drive "file=$disk,format=raw,if=none,id=disk"
 -device nvme,drive=disk,serial=common-v2-test
)
run(){
    local phase=$1 limit=$2 status=0
    timeout "$limit" "${qemu[@]}" -serial "file:$work/$phase.log" || status=$?
    if [[ "$status" != 0 && "$status" != 124 ]]; then
        tail -n 100 "$work/$phase.log" >&2 || true
        exit 1
    fi
}
run enroll 45
run boot 65
dd if="$disk" of="$work/after.img" bs=1M skip=1 count=64 status=none
if ! mcopy -i "$work/after.img" ::/EFI/InfiltratorOS/BOOT/BOOT.log "$work/BOOT.log"; then
    tail -n 120 "$work/"{enroll,boot}.log >&2 || true
    echo 'BOOT did not write its log under enrolled OVMF' >&2;exit 1
fi
iconv -f UTF-16LE -t UTF-8 "$work/BOOT.log" > "$work/log.txt"
cat "$work/log.txt"
tail -n 100 "$work/boot.log" || true
grep -Fq 'Common V2 authenticated reader: EXT4.so' "$work/log.txt"
grep -Fq 'Common V2 authenticated reader: NTFS.so' "$work/log.txt"
grep -Fq 'Common V2 module rejected: BAD.so' "$work/log.txt"
grep -Fq 'Common V2 module rejected: REVOKED.so' "$work/log.txt"
grep -Fq 'Common V2 module rejected: UNTRUSTED.so' "$work/log.txt"
test "$(grep -Fc 'Common V2 mounted volume on handle' "$work/log.txt")" = 1
grep -Fiq "Launching 'ext4_x64.efi'" "$work/log.txt"
grep -Fq 'EFIEXT4' "$work/log.txt"
grep -Fq 'NTFSV2' "$work/log.txt"
grep -Fq 'Entering main loop' "$work/log.txt"
grep -Fiq "Launching 'v2child.efi'" "$work/log.txt"
grep -aFq 'BOOT_V2_CHILD_READ_AND_RETURN_PASS' "$work/vars.fd"
dd if="$disk" of="$work/after-ext.img" bs=1M skip=65 count=64 status=none
dd if="$disk" of="$work/after-ntfs.img" bs=1M skip=129 count=64 status=none
cmp "$extimg" "$work/after-ext.img"
cmp "$ntimg" "$work/after-ntfs.img"
echo 'BOOT OVMF Secure Boot enrolled raw modules, EFI coexistence, NTFS child execution and unchanged media PASS'
