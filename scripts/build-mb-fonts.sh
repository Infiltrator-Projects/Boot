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
import math
import sys
from PIL import Image, ImageDraw, ImageFont

font_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])

glyphs = ''.join(chr(code) for code in range(32, 127)) + '?'
font = ImageFont.truetype(str(font_path), 30)

# Measure around a shared baseline so glyph shape, side bearings and vertical
# alignment remain those of the real MB face. Row zero of each fixed-width cell
# carries one byte of native FreeType advance plus an "IB" marker. Boot skips
# that metadata row when drawing, so the compatibility PNG remains self-contained
# without distorting proportional typography.
probe = Image.new('RGBA', (1, 1), (0, 0, 0, 0))
probe_draw = ImageDraw.Draw(probe)
boxes = [probe_draw.textbbox((0, 0), glyph, font=font, anchor='ls') for glyph in glyphs]
advances = [max(1, int(round(probe_draw.textlength(glyph, font=font)))) for glyph in glyphs]

min_left = min(left for left, top, right, bottom in boxes)
max_right = max(right for left, top, right, bottom in boxes)
min_top = min(top for left, top, right, bottom in boxes)
max_bottom = max(bottom for left, top, right, bottom in boxes)

pad_x = 4
pad_y = 3
origin_x = pad_x - min_left
baseline_y = 1 + pad_y - min_top
cell_width = origin_x + max_right + pad_x
cell_height = 1 + (max_bottom - min_top) + pad_y * 2

if cell_width > 255:
    raise SystemExit('firmware font cell is too wide for embedded advance metadata')
if max(advances) > 255:
    raise SystemExit('firmware font advance is too wide for embedded advance metadata')

atlas = Image.new('RGBA', (cell_width * len(glyphs), cell_height), (0, 0, 0, 0))
draw = ImageDraw.Draw(atlas)
for index, (glyph, advance) in enumerate(zip(glyphs, advances)):
    cell_x = index * cell_width
    # Metadata: R = native advance, G/B = "IB", A = 255. Runtime never renders row 0.
    atlas.putpixel((cell_x, 0), (advance, 0x49, 0x42, 255))
    draw.text((cell_x + origin_x, baseline_y), glyph, font=font, anchor='ls', fill=(0, 0, 0, 255))

out_path.parent.mkdir(parents=True, exist_ok=True)
atlas.save(out_path, format='PNG', optimize=False)

if atlas.width % 96 != 0:
    raise SystemExit('firmware font atlas width is not divisible by 96')
if atlas.mode != 'RGBA':
    raise SystemExit('firmware font atlas must retain an alpha channel')

# Re-open the PNG so CI generation itself proves that encoded metric bytes survive
# the actual PNG encoder/decoder path rather than only the in-memory image.
with Image.open(out_path).convert('RGBA') as check:
    decoded_cell = check.width // 96
    for index, expected in enumerate(advances):
        r, g, b, a = check.getpixel((index * decoded_cell, 0))
        if (r, g, b, a) != (expected, 0x49, 0x42, 255):
            raise SystemExit(f'font metric metadata did not survive PNG encoding at glyph {index}')

print(f'{out_path}: {atlas.width}x{atlas.height}, {cell_width}px cells, native proportional advances embedded')
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
rendering_contract=30px dark-base RGBA compatibility atlas with embedded native proportional advances and preserved baseline bearings
EOF
