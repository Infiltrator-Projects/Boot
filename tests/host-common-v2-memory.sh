#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -mno-red-zone -c boot/common_v2_memory.c -o "$tmp/memory.o"
test -z "$(nm -u "$tmp/memory.o")"
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_memory_test.c "$tmp/memory.o" -o "$tmp/test"
"$tmp/test"
