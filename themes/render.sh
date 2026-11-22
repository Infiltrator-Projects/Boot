#!/usr/bin/env bash
# Render the native 1920x1080 BOOT wallpaper source as a firmware PNG.
# No toolkit/renderer runs in EFI: only the resulting image is installed.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v rsvg-convert >/dev/null || {
    echo "Missing librsvg2-bin (rsvg-convert): cannot build BOOT theme." >&2
    exit 1
}
rsvg-convert --width 1920 --height 1080 --output themes/boot-luxury.png themes/boot-luxury.svg
[[ -s themes/boot-luxury.png ]] || exit 1
# PNG signature is fixed; reject a failed or accidentally renamed conversion.
signature=$(od -An -tx1 -N8 themes/boot-luxury.png | tr -d ' \n')
[[ "$signature" == 89504e470d0a1a0a ]] || {
    echo "BOOT theme image conversion did not produce PNG bytes." >&2
    exit 1
}
