#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
common=${COMMON_INCLUDE_DIR:?canonical Common include directory}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I"$common" -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -mno-red-zone -c boot/common_v2_namespace.c -o "$work/namespace.o"
test -z "$(nm -u "$work/namespace.o")"
cc -std=c11 -Wall -Wextra -Werror -I"$common" -fsanitize=address,undefined -fno-pie -no-pie tests/common_v2_namespace_test.c "$work/namespace.o" -o "$work/test"
"$work/test"
