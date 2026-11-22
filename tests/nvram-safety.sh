#!/usr/bin/env bash
# BOOT / Firmware variable tests
#
# Uses mocked firmware and efibootmgr output to check entry identity,
# ordering and idempotent registration.
#
# No test in this harness intentionally mutates the machine's real UEFI
# variables.

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
 -b) echo "Firmware refuses redundant activation of an active BOOT entry" >&2; exit 73 ;;
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

Problems=""
AddBootEntry || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
! command grep -q -- '^-b ' "$MOCK_NVRAM/actions" || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 1 || exit 1
# Count registration and ordering separately. The first install creates one
# firmware record. Reinstalling reuses its number but explicitly writes the
# order each time, even when BOOT is already first.
for i in {1..50}; do
    AddBootEntry || exit 1
done
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 51 || exit 1
test "$(command grep -c '^Boot' "$MOCK_NVRAM/entries")" = 3 || exit 1

echo "0001,0004,0002" > "$MOCK_NVRAM/order"
AddBootEntry || exit 1
test "$(cat "$MOCK_NVRAM/order")" = "0004,0001,0002" || exit 1
test "$(command grep -c -- '^-o ' "$MOCK_NVRAM/actions")" = 52 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1

echo "0004,NOPE,0002" > "$MOCK_NVRAM/order"
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Accepted invalid firmware BootOrder" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1

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
# An unreadable firmware listing and a successful listing with no matching
# entry have different consequences. This fixture checks that failed
# inspection cannot increase the number of create operations.
export MOCK_FAIL_READ=1
Problems=""
if AddBootEntry >/dev/null 2>&1; then
    echo "Installer ignored firmware inspection failure" >&2
    exit 1
fi
test "$Problems" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = "$creates_before" || exit 1
unset MOCK_FAIL_READ

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

command grep -Fq "Boot0001* Windows Boot Manager" "$MOCK_NVRAM/entries" || exit 1
command grep -Fq "Boot0002* Ubuntu" "$MOCK_NVRAM/entries" || exit 1
! command grep -Eq -- ' -B($| )' "$MOCK_NVRAM/actions" || exit 1
echo "BOOT is default on every install; missing, duplicate, or ambiguous firmware records cannot create extra entries"

# Regression: a Secure Boot entry created by versions up to 0.1.569
# passed an unterminated shim-name + BOOT-name pair. On Shim 15.8 this
# leaves the loader looking for grubx64.efi. Replace only the BOOT entry
# using a single correctly delimited UTF-16 argument; retain GRUB/Windows.
cat > "$work/bin/efibootmgr" <<'SHIM_MOCK'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$MOCK_NVRAM/actions"
case "${1:-}" in
 -v)
    printf 'BootOrder: %s\n' "$(cat "$MOCK_NVRAM/order")"
    cat "$MOCK_NVRAM/entries" ;;
 -b)
    [[ "${3:-}" == -B && "${2:-}" == 0004 ]] || exit 81
    command grep -v '^Boot0004' "$MOCK_NVRAM/entries" > "$MOCK_NVRAM/entries.tmp"
    mv "$MOCK_NVRAM/entries.tmp" "$MOCK_NVRAM/entries"
    echo 0001,0002 > "$MOCK_NVRAM/order" ;;
 -c)
    [[ "${@: -1}" == 'BOOTX64.EFI ' ]] || {
        echo "Shim second-stage argument missing its required final delimiter" >&2
        exit 82
    }
    [[ "$*" == *'-l \EFI\InfiltratorOS\BOOT\shimx64.efi'* ]] || exit 83
    printf '%s\n' 'Boot0007* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\shimx64.efi) File(.BOOTX64.EFI )' >> "$MOCK_NVRAM/entries"
    echo 0007,0001,0002 > "$MOCK_NVRAM/order" ;;
 -o)
    echo "$2" > "$MOCK_NVRAM/order" ;;
 '')
    printf 'BootOrder: %s\n' "$(cat "$MOCK_NVRAM/order")" ;;
 *)
    exit 84 ;;
esac
SHIM_MOCK
chmod +x "$work/bin/efibootmgr"
cat > "$MOCK_NVRAM/entries" <<'SHIM_ENTRIES'
Boot0001* ubuntu HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\ubuntu\shimx64.efi)
Boot0002* Windows Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\Microsoft\Boot\bootmgfw.efi)
Boot0004* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\shimx64.efi) File(.shimx64.efi BOOTX64.EFI)
SHIM_ENTRIES
echo 0004,0001,0002 > "$MOCK_NVRAM/order"
: > "$MOCK_NVRAM/actions"
KeepName=1
ShimSource=/boot/efi/EFI/ubuntu/shimx64.efi
TargetShim=shimx64.efi
TargetX64=BOOTX64.EFI
Boot=shimx64.efi
Problems=""
AddBootEntry || exit 1
test "$(cat "$MOCK_NVRAM/order")" = 0007,0001,0002 || exit 1
test "$(command grep -c '^Boot0007' "$MOCK_NVRAM/entries")" = 1 || exit 1
! command grep -q '^Boot0004' "$MOCK_NVRAM/entries" || exit 1
command grep -q '^Boot0001\* ubuntu' "$MOCK_NVRAM/entries" || exit 1
command grep -q '^Boot0002\* Windows Boot Manager' "$MOCK_NVRAM/entries" || exit 1
test "$(command grep -c -- '^-b ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
AddBootEntry || exit 1
test "$(command grep -c -- '^-b ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(command grep -c -- '^-c ' "$MOCK_NVRAM/actions")" = 1 || exit 1
test "$(cat "$MOCK_NVRAM/order")" = 0007,0001,0002 || exit 1
echo "Shim 15.8 filename handoff repaired without duplicating BOOT or changing GRUB/Windows"
