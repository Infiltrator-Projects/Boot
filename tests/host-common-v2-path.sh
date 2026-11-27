#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -mno-red-zone -c boot/common_v2_path.c -o "$tmp/path.o"
test -z "$(nm -u "$tmp/path.o")"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie tests/common_v2_path_test.c "$tmp/path.o" -o "$tmp/test"
"$tmp/test"
