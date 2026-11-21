#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Disposable block inode + fake efibootmgr, never an actual EFI variable.
set -euo pipefail
test "$EUID" -eq 0
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/bin" "$work/mock" "$work/esp"
block="$work/fake-partition"
mknod "$block" b 7 254
test -b "$block"
awk '/^GetParams "\$@"$/ {exit} {print}' boot-install > "$work/installer-functions.sh"
source "$work/installer-functions.sh"
InstallDir="$work/esp"
findmnt() { echo "$block"; }
readlink() { echo "$block"; }
lsblk() {
 case " $* " in
 *PKNAME*) echo loop42 ;;
 *PARTN*) echo 1 ;;
 *) return 2 ;;
 esac
}
blkid() { echo '12345678-1111-2222-3333-444455556666'; }
cat > "$work/bin/efibootmgr" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
echo "$*" >> "$MOCK_NVRAM/actions"
case "${1:-}" in
 -v) cat "$MOCK_NVRAM/entries" ;;
 -o) test ! -e "$MOCK_NVRAM/fail-order" || exit 1
     echo "$2" > "$MOCK_NVRAM/order" ;;
 -c) test "$*" = '-c -d /dev/loop42 -p 1 -l \EFI\InfiltratorOS\BOOT\BOOTX64.EFI -L BOOT Boot Manager'
     echo 'Boot0004* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)' >> "$MOCK_NVRAM/entries"
     echo '0004,0001,0002' > "$MOCK_NVRAM/order" ;;
 -b) echo "BOOT ENTRY DELETION FORBIDDEN" >&2; exit 99 ;;
 '') echo "BootOrder: $(cat "$MOCK_NVRAM/order")" ;;
 *) echo "Unexpected efibootmgr call: $*" >&2; exit 98 ;;
esac
MOCK
chmod +x "$work/bin/efibootmgr"
export MOCK_NVRAM="$work/mock" PATH="$work/bin:$PATH"
test "$(command -v efibootmgr)" = "$work/bin/efibootmgr"
cat > "$MOCK_NVRAM/entries" <<'ENTRIES'
Boot0001* Windows Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\Microsoft\Boot\bootmgfw.efi)
Boot0003* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)
Boot0002* Ubuntu HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\ubuntu\grubx64.efi)
ENTRIES
echo '0001,0003,0002' > "$MOCK_NVRAM/order"
# Embedded boot-install explicitly runs without strict shell settings.
set +e +u; set +o pipefail
AddBootEntry; code=$?
set -euo pipefail
test "$code" -eq 0
test "$(cat "$MOCK_NVRAM/order")" = '0003,0001,0002'
grep -Fq -- '-o 0003,0001,0002' "$MOCK_NVRAM/actions"
! grep -Eq '(^| )-b |(^| )-B($| )' "$MOCK_NVRAM/actions"
! grep -Eq '^-c' "$MOCK_NVRAM/actions"
# Register a new BOOT entry without touching Windows or GRUB.
grep -v 'BOOT Boot Manager' "$MOCK_NVRAM/entries" > "$MOCK_NVRAM/next"
mv "$MOCK_NVRAM/next" "$MOCK_NVRAM/entries"
echo '0001,0002' > "$MOCK_NVRAM/order"
: > "$MOCK_NVRAM/actions"
set +e +u; set +o pipefail
AddBootEntry; code=$?
set -euo pipefail
test "$code" -eq 0
test "$(cat "$MOCK_NVRAM/order")" = '0004,0001,0002'
grep -Fq -- '-c -d /dev/loop42 -p 1 -l \EFI\InfiltratorOS\BOOT\BOOTX64.EFI' "$MOCK_NVRAM/actions"
! grep -Eq '(^| )-b |(^| )-B($| )' "$MOCK_NVRAM/actions"
# On firmware command failure existing entries and their order stay unchanged.
cat > "$MOCK_NVRAM/entries" <<'ENTRIES'
Boot0003* BOOT Boot Manager HD(1,GPT,12345678-1111-2222-3333-444455556666)File(\EFI\InfiltratorOS\BOOT\BOOTX64.EFI)
ENTRIES
echo '0001,0003,0002' > "$MOCK_NVRAM/order"
: > "$MOCK_NVRAM/actions"
: > "$MOCK_NVRAM/fail-order"
Problems=""
set +e +u; set +o pipefail
AddBootEntry; code=$?
set -euo pipefail
test "$code" -ne 0
test "$Problems" = 1
test "$(cat "$MOCK_NVRAM/order")" = '0001,0003,0002'
! grep -Eq '(^| )-b |(^| )-B($| )' "$MOCK_NVRAM/actions"
echo "NVRAM mock: existing preserved, new registered, failures non-destructive"
