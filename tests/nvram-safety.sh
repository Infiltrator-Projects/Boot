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
 -v) [[ "${MOCK_FAIL_READ:-0}" == 0 ]] || exit 79
     printf 'BootOrder: %s\n' "$(cat "$MOCK_NVRAM/order")"
     cat "$MOCK_NVRAM/entries" ;;
 -c)
     [[ "${MOCK_FAIL_CREATE:-0}" == 0 ]] || exit 70
     [[ "$*" == *'-d /dev/nvme1n1 -p 1'* ]] || exit 71
     [[ "$*" == *'-l \EFI\InfiltratorOS\BOOT\BOOTX64.EFI'* ]] || exit 72
     id=$(cat "$MOCK_NVRAM/next")
     printf '%04X\n' "$((16#$id + 1))" > "$MOCK_NVRAM/next"
     printf '%s\n' "Boot${id}* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)" >> "$MOCK_NVRAM/entries"
     before=$(cat "$MOCK_NVRAM/order")
     printf '%s%s\n' "$id" "${before:+,$before}" > "$MOCK_NVRAM/order" ;;
 -b) [[ "${3:-}" == "-a" ]] || exit 73 ;;
 -o) [[ "${2:-}" =~ ^[0-9A-Fa-f]{4}(,[0-9A-Fa-f]{4})*$ ]] || exit 74
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

# First install: register one BOOT entry and put it first.
Problems=""
AddBootEntry || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 1 || exit 1

# Reinstall 50 times: no additional BOOT entries; explicitly set default each time.
for i in {1..50}; do
    AddBootEntry || exit 1
done
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 51 || exit 1
test "$(command grep -c '^Boot' "$MOCK_NVRAM/entries")" = 3 || exit 1

# If someone set Windows first, running BOOT again restores BOOT as default.
echo "0001,0004,0002" > "$MOCK_NVRAM/order"
AddBootEntry || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 52 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1

# Failure to set the default is a failed installation, not success.
# An invalid mock BootOrder is also rejected rather than silently accepted.
echo "0004,NOPE,0002" > "$MOCK_NVRAM/order"
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Accepted invalid firmware BootOrder" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1

# A duplicate BOOT entry must never trigger a third registration.
echo "0004,0001,0002" > "$MOCK_NVRAM/order"
printf '%s\n' 'Boot0007* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)' >> "$MOCK_NVRAM/entries"
creates_before=$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Installer accepted duplicate BOOT entries" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = "$creates_before" || exit 1

# If firmware listing fails, refuse creation rather than assuming BOOT absent.
export MOCK_FAIL_READ=1
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Installer ignored firmware inspection failure" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = "$creates_before" || exit 1
unset MOCK_FAIL_READ

# Even a BOOT-labelled entry on an unexpected EFI path is not a fresh install.
command grep -v '^Boot0007' "$MOCK_NVRAM/entries" | command grep -v '^Boot0004' > "$MOCK_NVRAM/entries.tmp"
mv "$MOCK_NVRAM/entries.tmp" "$MOCK_NVRAM/entries"
printf '%s\n' 'Boot0008* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\OldBoot\BOOTX64.EFI)' >> "$MOCK_NVRAM/entries"
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Installer created another BOOT entry with a stale BOOT entry present" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = "$creates_before" || exit 1
# Other OS entries are never deleted and are retained in BootOrder.
command grep -Fq "Boot0001* Windows Boot Manager" "$MOCK_NVRAM/entries" || exit 1
command grep -Fq "Boot0002* Ubuntu" "$MOCK_NVRAM/entries" || exit 1
! command grep -Eq -- ' -B($| )' "$MOCK_NVRAM/actions" || exit 1
echo "BOOT is default on every install; missing, duplicate, or ambiguous firmware records cannot create extra entries"
