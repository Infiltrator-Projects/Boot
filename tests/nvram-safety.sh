#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fake firmware tests: always register BOOT and set it first on install/reinstall.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/bin" "$work/mock" "$work/esp"
awk '/^RootDir=/ {inside=1} /^GetParams "\$@"$/ {exit} inside {print}' boot-native-installer.run > "$work/functions.sh"
source "$work/functions.sh"
InstallDir="$work/esp"
TargetDir=/EFI/InfiltratorOS/BOOT
Boot=BOOTX64.EFI
KeepName=0
ShimSource=none
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
case "${1:-}" in
 -v) cat "$MOCK_NVRAM/entries" ;;
 -c)
     [[ "${MOCK_FAIL_CREATE:-0}" == 0 ]] || exit 70
     [[ "$*" == *'-d /dev/nvme1n1 -p 1'* ]] || exit 71
     [[ "$*" == *'-l \EFI\InfiltratorOS\BOOT\BOOTX64.EFI'* ]] || exit 72
     id=$(cat "$MOCK_NVRAM/next")
     printf '%04X\n' "$((16#$id + 1))" > "$MOCK_NVRAM/next"
     printf '%s\n' "Bootlink_6aa9eb29612c8191b65880594e38ee41* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)" >> "$MOCK_NVRAM/entries"
     before=$(cat "$MOCK_NVRAM/order")
     printf '%s%s\n' "$id" "${before:+,$before}" > "$MOCK_NVRAM/order" ;;
 -b)
     [[ "${3:-}" == "-B" ]] || exit 73
     id="$2"
     # Removing another operating system is forbidden.
     [[ "$id" != 0001 && "$id" != 0002 ]] || exit 74
     command grep -Fq "Boot${id}* BOOT Boot Manager" "$MOCK_NVRAM/entries" || exit 75
     command grep -v "^Boot${id}\\*" "$MOCK_NVRAM/entries" > "$MOCK_NVRAM/entries.tmp"
     mv "$MOCK_NVRAM/entries.tmp" "$MOCK_NVRAM/entries"
     old=$(cat "$MOCK_NVRAM/order")
     next=""
     IFS=, read -r -a entries <<< "$old"
     for entry in "${entries[@]}"; do
         [[ "$entry" == "$id" ]] && continue
         next="${next:+$next,}$entry"
     done
     printf '%s\n' "$next" > "$MOCK_NVRAM/order" ;;
 -o)
     [[ "${MOCK_FAIL_ORDER:-0}" == 0 ]] || exit 76
     [[ "${2:-}" =~ ^[0-9A-Fa-f]{4}(,[0-9A-Fa-f]{4})*$ ]] || exit 77
     echo "$2" > "$MOCK_NVRAM/order" ;;
 '') printf 'BootOrder: %s\n' "$(cat "$MOCK_NVRAM/order")" ;;
 *) echo "Unexpected efibootmgr command: $*" >&2; exit 78 ;;
esac
MOCK
chmod +x "$work/bin/efibootmgr"
export MOCK_NVRAM="$work/mock" PATH="$work/bin:$PATH"
cat > "$MOCK_NVRAM/entries" <<'ENTRIES'
Boot0001* Windows Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\Microsoft\Boot\bootmgfw.efi)
Boot0002* Ubuntu HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\ubuntu\grubx64.efi)
ENTRIES
echo 0004 > "$MOCK_NVRAM/next"
echo 0001,0002 > "$MOCK_NVRAM/order"
: > "$MOCK_NVRAM/actions"
set +e +u
set +o pipefail

# Fresh installation: create BOOT, make it default without modifying other entries.
Problems=""
AddBootEntry > "$work/first.log" || exit 1
command grep -Fq "Registered BOOT as Boot0004; BOOT is first" "$work/first.log" || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
command grep -Fq -- "-o 0004,0001,0002" "$MOCK_NVRAM/actions" || exit 1

# Reinstall: replace the old BOOT entry even if it is already first.
AddBootEntry > "$work/second.log" || exit 1
command grep -Fq "Registered BOOT as Boot0005; BOOT is first" "$work/second.log" || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0005,0001,0002" || exit 1
! command grep -Fq "Boot0004*" "$MOCK_NVRAM/entries" || exit 1
command grep -Fq -- "-b 0004 -B" "$MOCK_NVRAM/actions" || exit 1

# Upgrade from a BOOT entry placed second: replace and set new entry first.
echo "0001,0005,0002" > "$MOCK_NVRAM/order"
AddBootEntry > "$work/third.log" || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0006,0001,0002" || exit 1
! command grep -Fq "Boot0005*" "$MOCK_NVRAM/entries" || exit 1
command grep -Fq -- "-b 0005 -B" "$MOCK_NVRAM/actions" || exit 1

# Creation failure must leave the previous, working boot entry in place.
export MOCK_FAIL_CREATE=1
before=$(cat "$MOCK_NVRAM/entries")
order_before=$(cat "$MOCK_NVRAM/order")
Problems=""
if AddBootEntry > "$work/failure.log" 2>&1; then
    echo "Accepted failed NVRAM creation" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1
test "$(cat "$MOCK_NVRAM/entries")" = "$before" || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "$order_before" || exit 1
unset MOCK_FAIL_CREATE

# Retaining Windows, GRUB and fallback firmware entries is mandatory.
command grep -Fq "Boot0001* Windows Boot Manager" "$MOCK_NVRAM/entries" || exit 1
command grep -Fq "Boot0002* Ubuntu" "$MOCK_NVRAM/entries" || exit 1
! command grep -Eq -- '-b (0001|0002) -B' "$MOCK_NVRAM/actions" || exit 1
! command grep -Fq "Keeping existing NVRAM entry" "$work/first.log" || exit 1
! command grep -Fq "Keeping existing NVRAM entry" "$work/second.log" || exit 1
echo "BOOT registration: fresh install, reinstall and upgrade set new BOOT first; other OSes preserved"
