#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Independent media preparation only; not linked into BOOT or installed.
set -euo pipefail
image=${1:?output image}; shift
provider=${OMFS_PROVIDER:?pinned independent OMFS source}
oracle=${OMFS_ORACLE:?pinned original OMFS formatter source}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
OMFS_ORACLE_BYTES=64M bash "$provider/tests/make-oracle-media.sh" "$oracle" "$tmp" "$@"
mv "$tmp/media.img" "$image"
