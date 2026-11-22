#!/usr/bin/env bash
# Compile and execute the actual BOOT source functions against deterministic
# disk metadata and icon-lookup mocks. No EFI disk or firmware writes occur.
set -euo pipefail
cd "$(dirname "$0")/.."

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
inc="$work/icon-detection-production.inc"

extract() {
    local signature="$1" file="$2"
    awk -v signature="$signature" '
      index($0,signature)==1 { active=1; count++ }
      active {
          print
          if ($0 ~ /^}[[:space:]]*$/) {
              complete=1
              exit
          }
      }
      END {
          if (count!=1 || !complete) exit 1
      }
    ' "$file" >> "$inc" || {
        printf 'Failed to extract %s from %s\n' "$signature" "$file" >&2
        exit 1
    }
}

# Definition order matters under strict C11 compilation; the functions are
# verbatim from production files, rather than rewritten approximations.
extract 'static VOID ParseReleaseFile(' boot/linux.c
extract 'VOID GuessLinuxDistribution(' boot/linux.c
extract 'static VOID AppendFirmwareOSIconHints(' boot/scan.c
extract 'VOID SetLoaderDefaults(' boot/scan.c

cc -std=c11 -O2 -Wall -Wextra -Werror \
    -I "$work" tests/icon-detection-regressions.c \
    -o "$work/icon-detection-regressions"
"$work/icon-detection-regressions"
