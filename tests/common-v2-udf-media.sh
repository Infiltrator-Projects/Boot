#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Independent disposable-media oracle only; not part of BOOT runtime.
set -euo pipefail
image=${1:?output image}; shift
(( $# && $# % 2 == 0 ))
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
while (( $# )); do
    [[ "$1" =~ ^[A-Za-z0-9_.-]+$ && "$1" != . && "$1" != .. ]]
    test -s "$2"
    cp "$2" "$work/$1"
    shift 2
done
genisoimage -quiet -udf -V V2UDF -o "$image" "$work"
test "$(stat -c %s "$image")" -le 67108864
truncate -s 64M "$image"
