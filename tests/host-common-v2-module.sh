#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
common=${COMMON_INCLUDE_DIR:?Set COMMON_INCLUDE_DIR to pinned Libraries include directory}
test -s "$common/infiltratr/filesystem_module_v2.h"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for bad in {0..9}; do
    cc -std=c11 -Wall -Wextra -Werror -I"$common" -shared -fPIC -fvisibility=hidden -ffreestanding -fcf-protection=none \
        -fno-stack-protector -mno-red-zone -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now,-z,nopack-relative-relocs,-z,max-page-size=4096 \
        -DBAD="$bad" tests/common_v2_module_fixture.c -o "$tmp/fixture$bad.so"
done
for name in elf storage module; do
    cc -std=c11 -Wall -Wextra -Werror -I"$common" -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-tree-loop-distribute-patterns -mno-red-zone -c "boot/common_v2_$name.c" -o "$tmp/$name.o"
done
test -z "$(nm -u "$tmp/module.o" | grep -v 'boot_v2_elf_contains' || true)"
cc -std=c11 -Wall -Wextra -Werror -I"$common" tests/common_v2_module_test.c "$tmp/"{elf,storage,module}.o -o "$tmp/test"
if ! "$tmp/test" "$tmp"/fixture{0..9}.so; then
    for fixture in "$tmp"/fixture{0..9}.so; do
        echo "Fixture $fixture"
        readelf -W -h -l -d -s -r "$fixture"
    done
    exit 1
fi
