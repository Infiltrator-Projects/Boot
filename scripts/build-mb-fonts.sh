#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$root/dist/fonts}"

source_commit="aa161e7342112beab8feb7669f072c870f742765"
archive_url="https://raw.githubusercontent.com/Infiltrator-Projects/MBLink/${source_commit}/assets/fonts/mb-corpo-fonts.tar.xz"
archive_sha256="bdb6063f838a7fab22b4d6b412170640c69511df53aa3dfa9a4ea8431c9d8274"

brand_regular="mb_corpo_a_cond_regular.ttf"
ui_bold="mb_corpo_s_bold.ttf"
ui_regular="mb_corpo_s_regular.ttf"

brand_regular_sha256="c8bcd7e1a7d71169b38491d9b7c1ffe7ba7b46e888f0c1219931343a47bc0e05"
ui_bold_sha256="d37ea986e2344d83390f94f170e6272b56efd00bfec808afe8314c4ca45d43b4"
ui_regular_sha256="94ede6629443c03d4362dcef425fb3ff520be5d654370021a34e81286804465c"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/extracted" "$out_dir"

curl --fail --location --silent --show-error --retry 3 \
    "$archive_url" -o "$work/mb-corpo-fonts.tar.xz"
printf '%s  %s\n' "$archive_sha256" "$work/mb-corpo-fonts.tar.xz" | sha256sum -c -
tar -xJf "$work/mb-corpo-fonts.tar.xz" -C "$work/extracted"

find_font() {
    local name="$1"
    find "$work/extracted" -type f -name "$name" -print -quit
}

brand_path="$(find_font "$brand_regular")"
bold_path="$(find_font "$ui_bold")"
regular_path="$(find_font "$ui_regular")"

test -n "$brand_path" && test -n "$bold_path" && test -n "$regular_path"
printf '%s  %s\n' "$brand_regular_sha256" "$brand_path" | sha256sum -c -
printf '%s  %s\n' "$ui_bold_sha256" "$bold_path" | sha256sum -c -
printf '%s  %s\n' "$ui_regular_sha256" "$regular_path" | sha256sum -c -

render_atlas() {
    local font_file="$1"
    local output_file="$2"
    python3 - "$font_file" "$output_file" <<'PY'
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

font_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])

# Boot's current renderer uses a fixed-width 96-glyph atlas: ASCII 32-126
# followed by one fallback glyph. Physical Dell firmware testing showed the
# previous 18px atlas was unnecessarily small, so render a 30px UI master while
# preserving the renderer's existing fixed-cell contract.
glyphs = ''.join(chr(code) for code in range(32, 127)) + '?'
font = ImageFont.truetype(str(font_path), 30)
probe = Image.new('RGBA', (1, 1), (0, 0, 0, 0))
probe_draw = ImageDraw.Draw(probe)
boxes = [probe_draw.textbbox((0, 0), glyph, font=font) for glyph in glyphs]
cell_width = max(right - left for left, top, right, bottom in boxes) + 4
ascent, descent = font.getmetrics()
cell_height = ascent + descent + 4

atlas = Image.new('RGBA', (cell_width * len(glyphs), cell_height), (0, 0, 0, 0))
draw = ImageDraw.Draw(atlas)
for index, (glyph, box) in enumerate(zip(glyphs, boxes)):
    left, top, right, bottom = box
    width = right - left
    height = bottom - top
    x = index * cell_width + (cell_width - width) // 2 - left
    y = (cell_height - height) // 2 - top
    draw.text((x, y), glyph, font=font, fill=(255, 255, 255, 255))

out_path.parent.mkdir(parents=True, exist_ok=True)
atlas.save(out_path, format='PNG', optimize=False)

if atlas.width % 96 != 0:
    raise SystemExit('firmware font atlas width is not divisible by 96')
if atlas.mode != 'RGBA':
    raise SystemExit('firmware font atlas must retain an alpha channel')
print(f'{out_path}: {atlas.width}x{atlas.height}, {atlas.width // 96}px cells')
PY
}

render_atlas "$regular_path" "$out_dir/mb-corpo-s-regular.png"
render_atlas "$bold_path" "$out_dir/mb-corpo-s-bold.png"
render_atlas "$brand_path" "$out_dir/mb-corpo-a-cond-regular.png"

cat > "$out_dir/FONT_PROVENANCE.txt" <<EOF
MB Corpo firmware font provenance
source_repository=Infiltrator-Projects/MBLink
source_commit=${source_commit}
archive_sha256=${archive_sha256}
ui_regular_file=${ui_regular}
ui_regular_sha256=${ui_regular_sha256}
ui_bold_file=${ui_bold}
ui_bold_sha256=${ui_bold_sha256}
brand_regular_file=${brand_regular}
brand_regular_sha256=${brand_regular_sha256}
EOF
