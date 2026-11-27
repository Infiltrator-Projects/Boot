#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The installed hosted carrier is never executed by the installer. Only its
# independently authenticated freestanding payload is copied to the ESP.
boot_v2_stage_modules() {
    local source="$1" stage="$2" verifier="$3" vars="$4" file base id
    [[ -d "$stage" && ! -L "$stage" && -x "$verifier" ]] || return 1
    [[ -d "$source" ]] || return 0
    local LC_ALL=C
    for file in "$source"/*; do
        [[ -f "$file" && ! -L "$file" ]] || continue
        base=${file##*/}
        base=${base,,}
        [[ "$base" =~ ^[a-z0-9][a-z0-9_-]{0,62}\.so$ ]] || continue
        id=${base%.so}
        if [[ -e "$stage/$id.so" ]]; then
            echo "BOOT: duplicate Common V2 module name: $base" >&2
            return 1
        fi
        if "$verifier" "$file" "$stage/$id.so" "$vars"; then
            echo "BOOT: authenticated firmware payload staged: $id.so"
        else
            echo "BOOT: excluding incompatible or unauthorized module: $file" >&2
        fi
    done
}
boot_v2_install_staged() {
    local stage="$1" dest="$2" file
    [[ -d "$stage" && ! -L "$stage" && -d "$dest" && ! -L "$dest" ]] || return 1
    for file in "$stage"/*.so; do
        [[ -f "$file" && ! -L "$file" ]] || continue
        install -m 0644 "$file" "$dest/${file##*/}" || return 1
        cmp -s "$file" "$dest/${file##*/}" || return 1
    done
}
