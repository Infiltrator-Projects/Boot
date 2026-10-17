#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dist="${1:?dist directory required}"
icons_dir="${2:?generated icons directory required}"
out_dir="${3:-$dist}"

# Pin the canonical Infiltrator design contract used to derive Boot's palette.
design_commit="e56f674f00e43afdfe6ac10a9936ad7ad0125f8e"
design_blob="ca96a0111607b6387b6b9dd389a4e90438efe5ed"
design_path="design/infiltrator-design-v1.json"
design_url="https://raw.githubusercontent.com/Infiltrator-Projects/Libraries/${design_commit}/${design_path}"

for required in \
    "$dist/mb-corpo-s-regular.png" \
    "$dist/mb-corpo-s-bold.png" \
    "$dist/mb-corpo-a-cond-regular.png" \
    "$icons_dir/os_infiltratoros.png"; do
    test -s "$required"
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out_dir" "$icons_dir"

curl --fail --location --silent --show-error --retry 3 \
    "$design_url" -o "$work/infiltrator-design-v1.json"
actual_blob="$(git hash-object "$work/infiltrator-design-v1.json")"
test "$actual_blob" = "$design_blob" || {
    echo "Infiltrator design identity mismatch: expected $design_blob, got $actual_blob" >&2
    exit 1
}

python3 - \
    "$work/infiltrator-design-v1.json" \
    "$icons_dir/os_infiltratoros.png" \
    "$dist/mb-corpo-s-bold.png" \
    "$dist/mb-corpo-a-cond-regular.png" \
    "$out_dir" \
    "$icons_dir" <<'PY'
from pathlib import Path
import json
import sys
from PIL import Image, ImageDraw

contract_path = Path(sys.argv[1])
os_icon_path = Path(sys.argv[2])
ui_bold_atlas_path = Path(sys.argv[3])
brand_atlas_path = Path(sys.argv[4])
out_dir = Path(sys.argv[5])
icons_dir = Path(sys.argv[6])

contract = json.loads(contract_path.read_text(encoding='utf-8'))
palette = contract['theme']['palettes']['night']


def rgba(value, alpha=255):
    value = value.lstrip('#')
    return tuple(int(value[i:i+2], 16) for i in (0, 2, 4)) + (alpha,)


BG = rgba(palette['background'])
PANEL = rgba(palette['panel'])
CARD = rgba(palette['card'])
BORDER = rgba(palette['border'])
TITLE = rgba(palette['title'])
TEXT = rgba(palette['text'])
MUTED = rgba(palette['muted'])
SUBTLE = rgba(palette['subtle'])
SELECTION = rgba(palette['selection_background'])
ACCENT = rgba(palette['neutral_accent'])


def atlas_text(atlas_path, text, colour, gap=3, scale=1.0):
    atlas = Image.open(atlas_path).convert('RGBA')
    if atlas.width % 96:
        raise SystemExit(f'{atlas_path} is not a 96-cell firmware atlas')
    cell = atlas.width // 96
    # Row zero is the IB metric header, not part of the font artwork.
    # Rendering it produced false dots above the product title and EFI glyphs.
    if not all(
        (atlas.getpixel((index * cell, 0))[1:4] == (0x49, 0x42, 255))
        for index in range(96)
    ):
        raise SystemExit(f'{atlas_path} lacks complete embedded IB metrics')
    glyph_top = 1
    pieces = []
    total_width = 0
    max_height = 1

    for character in text:
        index = ord(character) - 32 if 32 <= ord(character) < 127 else 95
        glyph = atlas.crop((index * cell, glyph_top, (index + 1) * cell, atlas.height))
        bbox = glyph.getchannel('A').getbbox()
        if bbox is None:
            width = max(4, (cell * 2) // 5)
            piece = Image.new('RGBA', (width, atlas.height), (0, 0, 0, 0))
        else:
            glyph = glyph.crop(bbox)
            alpha = glyph.getchannel('A')
            piece = Image.new('RGBA', glyph.size, colour)
            piece.putalpha(alpha)
        pieces.append(piece)
        total_width += piece.width
        max_height = max(max_height, piece.height)

    if pieces:
        total_width += gap * (len(pieces) - 1)
    image = Image.new('RGBA', (max(1, total_width), max_height), (0, 0, 0, 0))
    x = 0
    for piece in pieces:
        y = (max_height - piece.height) // 2
        image.alpha_composite(piece, (x, y))
        x += piece.width + gap

    if scale != 1.0:
        image = image.resize(
            (max(1, round(image.width * scale)), max(1, round(image.height * scale))),
            Image.Resampling.LANCZOS,
        )
    return image


# Product banner: show the canonical InfiltratorOS artwork alone.
# The product mark replaces the inherited Boot wordmark; it must not be
# accompanied by a separately rendered 'BOOT' label.
banner = Image.new('RGBA', (640, 120), BG)
logo = Image.open(os_icon_path).convert('RGBA')
logo_bbox = logo.getchannel('A').getbbox()
if logo_bbox:
    logo = logo.crop(logo_bbox)
logo.thumbnail((108, 108), Image.Resampling.LANCZOS)
banner.alpha_composite(logo, ((banner.width - logo.width) // 2,
                              (banner.height - logo.height) // 2))
banner.save(out_dir / 'boot-banner.png', format='PNG', optimize=False)


# Focus surfaces. The old embedded grey selection tiles are replaced by a calm
# Common night selection surface with a restrained Infiltrator accent edge.
def make_selection(size, radius, outline_width):
    image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    margin = max(4, outline_width + 2)
    draw.rounded_rectangle(
        (margin, margin, size - margin - 1, size - margin - 1),
        radius=radius,
        fill=rgba(palette['selection_background'], 244),
        outline=ACCENT,
        width=outline_width,
    )
    inner = margin + outline_width + 4
    if inner < size // 2:
        draw.rounded_rectangle(
            (inner, inner, size - inner - 1, size - inner - 1),
            radius=max(2, radius - outline_width - 2),
            outline=rgba(palette['border'], 180),
            width=1,
        )
    return image


make_selection(288, 24, 4).save(out_dir / 'boot-selection-big.png', format='PNG', optimize=False)
make_selection(128, 14, 3).save(out_dir / 'boot-selection-small.png', format='PNG', optimize=False)


# Utility glyphs are Boot chrome, so they should use one visual language rather
# than the inherited mixed artwork. OS logos remain OS-owned identities.
def utility_canvas(size=128):
    return Image.new('RGBA', (size, size), (0, 0, 0, 0))


def save_reset(path):
    image = utility_canvas()
    d = ImageDraw.Draw(image)
    box = (28, 28, 100, 100)
    d.arc(box, 35, 330, fill=TEXT, width=8)
    d.polygon([(25, 44), (48, 44), (36, 64)], fill=ACCENT)
    image.save(path, format='PNG', optimize=False)


def save_shutdown(path):
    image = utility_canvas()
    d = ImageDraw.Draw(image)
    d.arc((28, 28, 100, 100), 315, 225, fill=TEXT, width=8)
    d.rounded_rectangle((60, 20, 68, 66), radius=4, fill=ACCENT)
    image.save(path, format='PNG', optimize=False)


def save_firmware(path):
    image = utility_canvas()
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((34, 34, 94, 94), radius=10, outline=TEXT, width=7, fill=rgba(palette['surface'], 220))
    d.rounded_rectangle((51, 51, 77, 77), radius=5, outline=ACCENT, width=5)
    for p in (42, 56, 72, 86):
        d.line((p, 22, p, 34), fill=MUTED, width=4)
        d.line((p, 94, p, 106), fill=MUTED, width=4)
        d.line((22, p, 34, p), fill=MUTED, width=4)
        d.line((94, p, 106, p), fill=MUTED, width=4)
    image.save(path, format='PNG', optimize=False)


save_reset(icons_dir / 'func_reset.png')
save_shutdown(icons_dir / 'func_shutdown.png')
save_firmware(icons_dir / 'func_firmware.png')


# Neutral EFI/unknown candidates should not look like inherited multicolour demo
# art. They use product-neutral Common surfaces; recognised OS logos stay intact.
def neutral_candidate(label, path):
    size = 256
    image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.rounded_rectangle((28, 28, 228, 228), radius=44, fill=CARD, outline=BORDER, width=4)
    d.rounded_rectangle((42, 42, 214, 214), radius=34, outline=rgba(palette['selection_background']), width=2)
    text = atlas_text(ui_bold_atlas_path, label, TITLE, gap=2, scale=1.75)
    text.thumbnail((130, 82), Image.Resampling.LANCZOS)
    image.alpha_composite(text, ((size - text.width) // 2, (size - text.height) // 2 - 4))
    d.rounded_rectangle((74, 190, 182, 194), radius=2, fill=ACCENT)
    image.save(path, format='PNG', optimize=False)


neutral_candidate('EFI', icons_dir / 'os_uefi.png')
neutral_candidate('?', icons_dir / 'os_unknown.png')

# Basic structural checks before returning assets to the release pipeline.
for name, expected in {
    'boot-banner.png': (640, 120),
    'boot-selection-big.png': (288, 288),
    'boot-selection-small.png': (128, 128),
}.items():
    path = out_dir / name
    with Image.open(path) as check:
        if check.size != expected or check.mode != 'RGBA':
            raise SystemExit(f'{name} has unexpected image contract: {check.size} {check.mode}')
PY

cat > "$out_dir/THEME_PROVENANCE.txt" <<EOF
Boot firmware theme provenance
design_repository=Infiltrator-Projects/Libraries
design_commit=${design_commit}
design_path=${design_path}
design_blob=${design_blob}
design_contract=infiltrator-design-v1
theme_palette=night
theme_background=#050608
theme_panel=#101318
theme_card=#171B20
theme_border=#353A40
theme_text=#E8ECEF
theme_title=#EEF1F3
theme_muted=#AEB6BD
theme_selection=#2B3137
theme_accent=#00ADEF
EOF

for generated in \
    "$out_dir/boot-banner.png" \
    "$out_dir/boot-selection-big.png" \
    "$out_dir/boot-selection-small.png" \
    "$icons_dir/func_reset.png" \
    "$icons_dir/func_shutdown.png" \
    "$icons_dir/func_firmware.png" \
    "$icons_dir/os_uefi.png" \
    "$icons_dir/os_unknown.png"; do
    test -s "$generated"
done

printf 'Built canonical Boot firmware theme from Libraries %s\n' "$design_commit"
