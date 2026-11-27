#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror packaging/common-v2-verify.c boot/common_v2_elf.c boot/common_v2_trust.c -o "$work/verify"
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_esl.c boot/common_v2_trust.c -o "$work/esl"
flags=(-shared -fPIC -fvisibility=hidden -ffreestanding -fcf-protection=none -fno-stack-protector -mno-red-zone -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now,-z,nopack-relative-relocs,-z,max-page-size=4096)
cc "${flags[@]}" tests/common_v2_elf_fixture.c -o "$work/raw.so"
cc "${flags[@]}" -DTEST_MIXED_ABI tests/common_v2_elf_fixture.c -o "$work/mixed.so"
mkdir "$work/vars"
vars="$work/vars"
db="$vars/db-d719b2cb-3d3a-4596-a3bc-dad00e67656f"
dbx="$vars/dbx-d719b2cb-3d3a-4596-a3bc-dad00e67656f"
setup="$vars/SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c"
printf '\006\000\000\000\000' > "$setup"
printf 'not an ELF\n' > "$work/bad.so"
{ printf '\047\000\000\000'; "$work/esl" "$work/raw.so"; "$work/esl" "$work/mixed.so"; } > "$db"
{ printf '\047\000\000\000'; "$work/esl" "$work/bad.so"; } > "$dbx"
"$work/verify" "$work/raw.so" "$work/output.so" "$vars"
cmp "$work/raw.so" "$work/output.so"
# A mixed-ABI outer carrier must never be executed or copied.
objcopy --add-section ".infiltrator_boot=$work/raw.so" --set-section-flags .infiltrator_boot=readonly "$work/mixed.so" "$work/carrier.so"
"$work/verify" "$work/carrier.so" "$work/extracted.so" "$vars"
cmp "$work/raw.so" "$work/extracted.so"
reject() {
    if "$work/verify" "$1" "$work/rejected.so" "$vars"; then
        echo "Unexpected acceptance: $1" >&2; exit 1
    fi
    test ! -e "$work/rejected.so"
}
reject "$work/mixed.so"
reject "$work/bad.so"
cp "$work/raw.so" "$work/changed.so"
printf '\001' | dd of="$work/changed.so" bs=1 seek=8 conv=notrunc status=none
reject "$work/changed.so"
ln -s "$work/raw.so" "$work/symlink.so"
reject "$work/symlink.so"
cp "$dbx" "$work/dbx.saved"
{ printf '\047\000\000\000'; "$work/esl" "$work/raw.so"; } > "$dbx"
reject "$work/raw.so"
mv "$work/dbx.saved" "$dbx"
printf '\006\000\000\000\001' > "$setup"
reject "$work/raw.so"
printf '\006\000\000\000\000' > "$setup"
echo 'BOOT test-only carrier extraction, authentication and rejection PASS'
