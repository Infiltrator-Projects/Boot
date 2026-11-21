#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise original BOOT NVMe registration with fake firmware only.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/bin" "$work/mock" "$work/esp"
awk '/^RootDir=/ {inside=1} /^GetParams "\$@"$/ {exit} inside {print}' boot-native-installer.run > "$work/functions.sh"
source "$work/functions.sh"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
Boot=BOOTX64.EFI
grep() {
    if [[ "$#" -ge 2 && "$2" == /etc/mtab ]]; then
        printf '/dev/nvme1n1p1 %s vfat rw,relatime 0 0\n' "$InstallDir"
    else
        command grep "$@"
    fi
}
lsblk() { printf 'NAME MAJ:MIN RM SIZE RO TYPE MOUNTPOINTS\nnvme1n1 259:0 0 953G 0 disk\nnvme1n1p1 259:1 0 512M 0 part /boot/efi\n'; }
blkid() { printf '12345678-1111-2222-3333-444455556666\n'; }
cat > "$work/bin/efibootmgr" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$MOCK_NVRAM/actions"
case "$1" in
 -v) cat "$MOCK_NVRAM/entries" ;;
 -c) [[ "$*" == *'-d /dev/nvme1n1 -p 1'* ]] || exit 70
     echo 'Boot0004* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)' >> "$MOCK_NVRAM/entries"
     echo '0004,0001,0002' > "$MOCK_NVRAM/order" ;;
 -b) echo 'Unexpected modification of existing NVRAM entry' >&2; exit 71 ;;
 '') printf 'BootOrder: %s\n' "$(cat "$MOCK_NVRAM/order")" ;;
 *) echo "Unexpected efibootmgr call: $*" >&2; exit 72 ;;
esac
MOCK
chmod +x "$work/bin/efibootmgr"
export MOCK_NVRAM="$work/mock" PATH="$work/bin:$PATH"
cat > "$MOCK_NVRAM/entries" <<'ENTRIES'
Boot0001* Windows Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\Microsoft\Boot\bootmgfw.efi)
Boot0002* Ubuntu HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\ubuntu\grubx64.efi)
ENTRIES
echo '0001,0002' > "$MOCK_NVRAM/order"
set +e +u
set +o pipefail
AddBootEntry; code=$?
test "$code" -eq 0 || exit 1
command grep -Fq -- '-c -l \EFI\InfiltratorOS\BOOT\BOOTX64.EFI -L BOOT Boot Manager -d /dev/nvme1n1 -p 1' "$MOCK_NVRAM/actions" || exit 1
! command grep -Eq '(^| )-b |(^| )-B($| )' "$MOCK_NVRAM/actions" || exit 1
command grep -Fq 'Boot0001* Windows Boot Manager' "$MOCK_NVRAM/entries" || exit 1
command grep -Fq 'Boot0002* Ubuntu' "$MOCK_NVRAM/entries" || exit 1
echo "Historical BOOT NVMe registration mocked; Windows and GRUB retained"
