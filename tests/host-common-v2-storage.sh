#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -mno-red-zone -c boot/common_v2_storage.c -o "$tmp/storage.o"
test -z "$(nm -u "$tmp/storage.o")"
cc -std=c11 -Wall -Wextra -Werror tests/common_v2_storage_test.c "$tmp/storage.o" -o "$tmp/test"
"$tmp/test"
