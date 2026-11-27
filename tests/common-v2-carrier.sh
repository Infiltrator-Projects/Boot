#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Validate the exact embedded firmware image from the owner's actual carrier.
# All trust variables below are disposable test fixtures, never host efivars.
set -euo pipefail
carrier=$(realpath "${1:?provider carrier}")
name=${2:?provider name}
tester=$(realpath "${3:?compiled provider tester}")
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
objcopy --dump-section .infiltrator_boot="$work/expected.so" "$carrier" "$work/carrier-copy.so"
test -s "$work/expected.so"
cc -std=c11 -O2 -Wall -Wextra -Werror packaging/common-v2-verify.c boot/common_v2_elf.c boot/common_v2_trust.c -o "$work/verify"
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_esl.c boot/common_v2_trust.c -o "$work/esl"
mkdir "$work/vars"
db="$work/vars/db-d719b2cb-3d3a-4596-a3bc-dad00e67656f"
dbx="$work/vars/dbx-d719b2cb-3d3a-4596-a3bc-dad00e67656f"
setup="$work/vars/SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c"
printf '\006\000\000\000\000' > "$setup"
printf 'unrelated revocation fixture\n' > "$work/unrelated"
{ printf '\047\000\000\000'; "$work/esl" "$work/expected.so"; } > "$db"
{ printf '\047\000\000\000'; "$work/esl" "$work/unrelated"; } > "$dbx"
"$work/verify" "$carrier" "$work/extracted.so" "$work/vars"
cmp "$work/expected.so" "$work/extracted.so"
"$tester" "$work/extracted.so" "$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
objdump -d "$work/extracted.so" > "$work/disassembly"
if grep -E '(%[xyz]mm[0-9]+|%st\(|%mm[0-9]+|%k[0-7]([,[:space:]]|$))' "$work/disassembly"; then
    echo 'Packaged firmware module uses extended register state' >&2
    exit 1
fi
# The real installer must refuse this very same payload once revoked.
{ printf '\047\000\000\000'; "$work/esl" "$work/expected.so"; } > "$dbx"
if "$work/verify" "$carrier" "$work/revoked.so" "$work/vars"; then
    echo 'Unexpected acceptance of revoked carrier' >&2
    exit 1
fi
test ! -e "$work/revoked.so"
echo "$name exact release-target carrier extraction, authentication, ABI and revocation PASS"
