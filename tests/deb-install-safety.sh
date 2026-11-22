#!/usr/bin/env bash
# Exercise the actual generic .deb payload and its install functions against
# disposable ESP and NVRAM fixtures without touching the host firmware.
set -euo pipefail

deb="${1:?Usage: tests/deb-install-safety.sh PACKAGE.deb}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
dpkg-deb -x "$deb" "$work/unpacked"
dpkg-deb -e "$deb" "$work/control"
source "$work/unpacked/usr/lib/infiltrator-boot/install-generic.sh"
bash -n "$work/unpacked/usr/lib/infiltrator-boot/install-generic.sh"
test -x "$work/control/postinst"
grep -Fq 'exec /usr/lib/infiltrator-boot/install-generic.sh' "$work/control/postinst"

source_dir="$work/unpacked/usr/share/infiltrator-boot/boot"
esp="$work/esp"
boot_dir="$esp/EFI/InfiltratorOS/BOOT"
mkdir -p "$esp/EFI/Microsoft/Boot" "$esp/EFI/ubuntu" "$esp/EFI/BOOT" "$boot_dir/icons"
printf 'Windows\n' > "$esp/EFI/Microsoft/Boot/bootmgfw.efi"
printf 'GRUB\n' > "$esp/EFI/ubuntu/grubx64.efi"
printf 'fallback\n' > "$esp/EFI/BOOT/BOOTX64.EFI"
printf 'old\n' > "$boot_dir/BOOTX64.EFI"
printf 'old config\n' > "$boot_dir/boot.conf"
printf 'old icon\n' > "$boot_dir/icons/old.png"
printf 'unowned\n' > "$boot_dir/arbitrary-file"
boot_generic_install_files "$esp" "$source_dir"
cmp "$source_dir/boot_x64.efi" "$boot_dir/BOOTX64.EFI"
cmp "$source_dir/boot.conf-sample" "$boot_dir/boot.conf"
while read -r digest face; do
    cmp "$source_dir/fonts/$face" "$boot_dir/fonts/$face"
done < "$source_dir/fonts/SHA256SUMS"
( cd "$boot_dir/fonts" && sha256sum -c SHA256SUMS >/dev/null )
test ! -e "$boot_dir/icons/old.png"
test ! -e "$boot_dir/arbitrary-file"
test ! -e "$boot_dir/.infiltrator-boot-loader.sha256"
test "$(cat "$esp/EFI/Microsoft/Boot/bootmgfw.efi")" = Windows
test "$(cat "$esp/EFI/ubuntu/grubx64.efi")" = GRUB
test "$(cat "$esp/EFI/BOOT/BOOTX64.EFI")" = fallback
for fs in ext2 ext4 reiserfs iso9660 hfs btrfs; do
    cmp "$source_dir/drivers_x64/${fs}_x64.efi" "$boot_dir/drivers_x64/${fs}_x64.efi"
done

# Failure to validate the staged image must not delete a working installation.
cp -a "$source_dir" "$work/incomplete"
rm -f "$work/incomplete/drivers_x64/ext4_x64.efi"
if boot_generic_install_files "$esp" "$work/incomplete"; then
    echo "Generic installer accepted an incomplete driver set." >&2
    exit 1
fi
cmp "$source_dir/boot_x64.efi" "$boot_dir/BOOTX64.EFI"
if boot_generic_install_files "$work/not-mounted" "$source_dir"; then
    echo "Generic installer installed outside a mounted ESP." >&2
    exit 1
fi
# An EFI symlink is forbidden, even when its target is another test directory.
mkdir -p "$work/linked"
ln -s "$esp/EFI" "$work/linked/EFI"
if boot_generic_install_files "$work/linked" "$source_dir"; then
    echo "Generic installer followed an EFI directory symlink." >&2
    exit 1
fi

mkdir -p "$work/bin" "$work/nvram"
cat > "$work/bin/efibootmgr" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}" in
    -c)
        [[ "$*" == *'-d /dev/nvme0n1 -p 1'* ]] || exit 80
        [[ "$*" == *'\EFI\InfiltratorOS\BOOT\BOOTX64.EFI'* ]] || exit 81
        echo create >> "$BOOT_TEST_NVRAM/calls"
        printf 'Boot0007* BOOT Boot Manager HD(1,GPT,%s,0x800,0x100000)File(\\EFI\\InfiltratorOS\\BOOT\\BOOTX64.EFI)\n' "$BOOT_TEST_UUID" >> "$BOOT_TEST_NVRAM/entries"
        ;;
    -b)
        [[ "$2" == 0007 && "$3" == -a ]] || exit 82
        echo activate >> "$BOOT_TEST_NVRAM/calls"
        ;;
    -o)
        printf '%s\n' "$2" > "$BOOT_TEST_NVRAM/order"
        echo priority >> "$BOOT_TEST_NVRAM/calls"
        ;;
    -v|"")
        printf 'BootOrder: %s\n' "$(cat "$BOOT_TEST_NVRAM/order")"
        cat "$BOOT_TEST_NVRAM/entries"
        ;;
    *) exit 83 ;;
esac
MOCK
chmod 0755 "$work/bin/efibootmgr"
export BOOT_TEST_NVRAM="$work/nvram"
export BOOT_TEST_UUID="12345678-1111-2222-3333-444455556666"
export PATH="$work/bin:$PATH"
printf '0001,0002\n' > "$BOOT_TEST_NVRAM/order"
printf 'Boot0001* Windows Boot Manager File(\\EFI\\Microsoft\\Boot\\bootmgfw.efi)\nBoot0002* Ubuntu File(\\EFI\\ubuntu\\grubx64.efi)\n' > "$BOOT_TEST_NVRAM/entries"
boot_generic_register /dev/nvme0n1 1 "$BOOT_TEST_UUID"
boot_generic_register /dev/nvme0n1 1 "$BOOT_TEST_UUID"
test "$(grep -c '^create$' "$BOOT_TEST_NVRAM/calls")" -eq 1
test "$(cat "$BOOT_TEST_NVRAM/order")" = "0007,0001,0002"
printf 'Boot0008* BOOT Boot Manager HD(1,GPT,%s)File(\\EFI\\InfiltratorOS\\BOOT\\BOOTX64.EFI)\n' "$BOOT_TEST_UUID" >> "$BOOT_TEST_NVRAM/entries"
if boot_generic_register /dev/nvme0n1 1 "$BOOT_TEST_UUID"; then
    echo "Generic installer accepted duplicate BOOT firmware entries." >&2
    exit 1
fi
test "$(grep -c '^create$' "$BOOT_TEST_NVRAM/calls")" -eq 1
echo "Generic .deb installs BOOT and six EFI drivers under the fixed path, preserves independent loaders, and registers BOOT once."
