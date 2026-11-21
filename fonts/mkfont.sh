#!/usr/bin/env bash


























if [[ $# != 4 ]] ; then
   echo "Usage: $0 font-name font-size y-offset bitmap-filename.png"
   echo "   font-name: Name of font (use 'convert -list font | less' to get list)"
   echo "              NOTE: Font MUST be monospaced!"
   echo "   font-size: Font size in points"
   echo "   y-offset: pixels font is shifted (may be negative)"
   echo "   bitmap-filename.png: output filename"
   echo ""
   exit 1
fi

Convert="$(command -v convert 2> /dev/null)"
if [[ ! -x $Convert ]] ; then
   echo "The 'convert' program is required but could not be found. It's part of the"
   echo "ImagMagick program, usually installed in the 'imagemagick' package."
   echo ""
   exit 1
fi

Height=$2
let CellWidth=(${Height}*6+5)/10

let Width=${CellWidth}*96
echo "Creating ${Width}x${Height} font bitmap...."
$Convert -size ${Width}x${Height} xc:transparent -gravity NorthWest -font $1 -pointsize $2 \
      -draw "text 0,$3 ' !\"#\$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_\`abcdefghijklmnopqrstuvwxyz{|}~?'" $4
