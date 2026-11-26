#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -shared -fPIC -fvisibility=hidden -ffreestanding -fno-stack-protector -mno-red-zone -nostdlib \
    -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now tests/common_v2_elf_fixture.c -o "$tmp/fixture.so"
cc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-tree-loop-distribute-patterns -mno-red-zone -c boot/common_v2_elf.c -o "$tmp/loader.o"
test -z "$(nm -u "$tmp/loader.o")"
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_elf_test.c "$tmp/loader.o" -o "$tmp/test"
"$tmp/test" "$tmp/fixture.so"
