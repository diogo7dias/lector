#!/bin/bash
# Narrow-scope conversion: regenerate ONLY the Cozette UI font bitmaps. Does NOT
# invoke convert-builtin-fonts.sh or touch any reader font.
#
# UI font = Cozette (https://github.com/slavfox/Cozette, MIT), vector build.
# Lector uses Cozette as the UI face in place of CrossPoint's Ubuntu. Three
# distinct sizes at the historic ppem (fontconvert.py hardcodes dpi 150 in
# set_char_size -> ppem = size * 150/72):
#
#   cozette_10  CozetteVector  size 10  (~21px)  -> status bar / hints   (SMALL_FONT_ID)
#   cozette_12  CozetteVector  size 12  (~25px)  -> body / menus / rows   (UI_10_FONT_ID)
#   cozette_14  CozetteVector  size 14  (~29px)  -> titles / headers      (UI_12_FONT_ID)
#
# Cozette ships regular only here, so there is no bold face; the UI weight
# hierarchy comes from the three sizes. main.cpp passes the regular face into the
# family's bold slot so a BOLD request (e.g. Lyra card titles) resolves to
# regular Cozette rather than nothing.
#
# Weight: baked at --bw-threshold 8 (true regular weight). Cozette is a pixel
# font; rendered off its native grid it grows AA halos on the pixel edges, and
# the default threshold 2 ("any coverage = black") turns those halos black and
# fattens the face. Threshold 8 (~50% coverage) keeps it thin/clean. 1-bit, no
# --2bit (UI is pure black/white), no --pnum (Cozette has no proportional nums).
set -e
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SRC="../builtinFonts/source/UI"
if [[ ! -f "$SRC/CozetteVector.ttf" ]]; then
  echo "error: missing source font: $SRC/CozetteVector.ttf" >&2
  exit 1
fi

gen() { # <out-header-name> <size>   (dpi 150 hardcoded in fontconvert.py; threshold 8 = regular weight)
  echo "Generating $1 from CozetteVector.ttf @ size ${2} (threshold 8)..."
  "$PYTHON_BIN" fontconvert.py "$1" "$2" "$SRC/CozetteVector.ttf" --bw-threshold 8 > "../builtinFonts/$1.h"
}

gen cozette_10 10   # status bar / hints (SMALL_FONT_ID)
gen cozette_12 12   # body / menus / rows (UI_10_FONT_ID)
gen cozette_14 14   # titles / headers (UI_12_FONT_ID)

echo ""
echo "Generated 3 Cozette UI headers (sizes 10/12/14, --bw-threshold 8)."
