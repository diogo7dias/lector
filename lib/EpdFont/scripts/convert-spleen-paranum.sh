#!/bin/bash
# Narrow-scope conversion: regenerate ONLY the paragraph-number font bitmap.
# Does NOT invoke convert-builtin-fonts.sh and does NOT touch any UI or reader font.
#
# Paragraph-number font = Spleen 6x12 (https://github.com/fcambus/spleen, BSD-2-Clause).
# Used exclusively by EpubReaderActivity::drawParagraphNumbers via PARA_NUM_FONT_ID.
#
#   spleen_6x12_regular  spleen-6x12.otf  size 12 at --dpi 72  ->  12px em, 8px digits
#
# The --dpi 72 is the whole point. fontconvert.py defaults to dpi 150, where "size N"
# means N * 150 / 72 pixels, so a bitmap font never lands on its own pixel grid and
# FreeType anti-aliases every stem into a halo. At dpi 72, size 12 is exactly 12 pixels,
# which is Spleen's native cell height: one-pixel stems, pure black and white, no smear.
# This is why the digits stay legible at a size smaller than the old Cozette numbers.
#
# 1-bit (no --2bit): margin numbers are pure black on white, greyscale buys nothing.
# No --compress: that path requires --2bit.
set -e
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SRC="../builtinFonts/source/Spleen"
if [[ ! -f "$SRC/spleen-6x12.otf" ]]; then
  echo "error: missing source font: $SRC/spleen-6x12.otf" >&2
  exit 1
fi

echo "Generating spleen_6x12_regular from spleen-6x12.otf @ size 12, dpi 72..."
"$PYTHON_BIN" fontconvert.py spleen_6x12_regular 12 "$SRC/spleen-6x12.otf" --dpi 72 \
  > "../builtinFonts/spleen_6x12_regular.h"

echo ""
echo "Generated ../builtinFonts/spleen_6x12_regular.h"
echo "If the bytes changed, re-run ./build-font-ids.sh so PARA_NUM_FONT_ID matches."
