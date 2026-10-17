#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$root/dist/icons}"

# Product-owned InfiltratorOS artwork. Pin both the source commit and Git blob
# identity so the firmware icon cannot silently change underneath a Boot build.
source_commit="25575739132e0450e7d12e6c696bcc4ad0de168c"
source_blob="baeb7562ee620554a2e0dfcf6df914d2f82be105"
source_path="assets/infiltratoros.png"
source_url="https://raw.githubusercontent.com/Infiltrator-Projects/InfiltratorOS/${source_commit}/${source_path}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out_dir"

curl --fail --location --silent --show-error --retry 3 \
    "$source_url" -o "$work/infiltratoros.png"
actual_blob="$(git hash-object "$work/infiltratoros.png")"
test "$actual_blob" = "$source_blob" || {
    echo "InfiltratorOS artwork identity mismatch: expected $source_blob, got $actual_blob" >&2
    exit 1
}

# Keep the mature functional/third-party OS glyphs that Boot already knows how
# to address, but do not distribute historical boot-manager product identities.
# InfiltratorOS gets its own icon below from the product-owned artwork.
find "$root/icons" -maxdepth 1 -type f -name '*.png' -print0 |
while IFS= read -r -d '' icon; do
    case "$(basename "$icon")" in
        os_refind.png|os_refit.png) continue ;;
    esac
    cp "$icon" "$out_dir/"
done

python3 - "$work/infiltratoros.png" "$out_dir/os_infiltratoros.png" <<'PY'
from pathlib import Path
import sys
from PIL import Image

source = Path(sys.argv[1])
destination = Path(sys.argv[2])

image = Image.open(source).convert('RGBA')
alpha = image.getchannel('A')
bbox = alpha.getbbox()
if bbox:
    image = image.crop(bbox)

# A 256px transparent master remains crisp when Boot doubles icon sizes on
# HiDPI displays; the firmware renderer scales it down for normal 128px menus.
image.thumbnail((224, 224), Image.Resampling.LANCZOS)
canvas = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
x = (canvas.width - image.width) // 2
y = (canvas.height - image.height) // 2
canvas.alpha_composite(image, (x, y))
destination.parent.mkdir(parents=True, exist_ok=True)
canvas.save(destination, format='PNG', optimize=False)

with Image.open(destination) as check:
    if check.size != (256, 256) or check.mode != 'RGBA':
        raise SystemExit('generated InfiltratorOS icon is not a 256x256 RGBA PNG')
PY

# Alias the same product artwork to the hints Boot can derive from common
# loader-directory and partition names before the target root filesystem is
# necessarily readable.
cp "$out_dir/os_infiltratoros.png" "$out_dir/os_infiltrator.png"
cp "$out_dir/os_infiltratoros.png" "$out_dir/os_infiltratorboot.png"

# Preserve icon licensing/provenance with the runtime bundle.
cp "$root/icons/README" "$out_dir/README"
if [[ -d "$root/icons/licenses" ]]; then
    cp -R "$root/icons/licenses" "$out_dir/licenses"
fi

source_sha256="$(sha256sum "$work/infiltratoros.png" | awk '{print $1}')"
icon_sha256="$(sha256sum "$out_dir/os_infiltratoros.png" | awk '{print $1}')"
cat > "$out_dir/ICON_PROVENANCE.txt" <<EOF
Boot icon provenance
source_repository=Infiltrator-Projects/InfiltratorOS
source_commit=${source_commit}
source_path=${source_path}
source_blob=${source_blob}
source_sha256=${source_sha256}
generated_icon=os_infiltratoros.png
generated_icon_sha256=${icon_sha256}
aliases=os_infiltrator.png,os_infiltratorboot.png
EOF

printf 'Built %s PNG icons; InfiltratorOS icon SHA-256 %s\n' \
    "$(find "$out_dir" -maxdepth 1 -type f -name '*.png' | wc -l)" "$icon_sha256"
