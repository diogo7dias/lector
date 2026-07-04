#!/bin/bash
#
# Bake the Lector reader font set into 2-bit greyscale flash headers.
#
# Families (per approved Lector re-fork design, 2026-07-04):
#   Bookerly (default), Georgia, Verdana  -> Regular / Italic / Bold  (no real bold-italic source)
#   Merriweather                          -> Regular / Italic / Bold / BoldItalic (has real BI)
#
# Sizes 11-16 (six sizes). 2-bit is deliberate: CrossPoint's greyscale glyph AA is the
# reason for the re-fork, so we keep it rather than trade it for flash.
#
# Output naming matches CrossPoint's convention: <family>_<size>_<style>.h
# Only python3 exists on this machine (scripts elsewhere say `python`).

set -e

cd "$(dirname "$0")"

SIZES=(11 12 13 14 15 16)

bake() { # <family_lc> <size> <style_lc> <ttf_path>
  local name="$1_$2_$3"
  local out="../builtinFonts/${name}.h"
  python3 fontconvert.py "$name" "$2" "$4" --2bit --compress --pnum > "$out"
  echo "Generated $out"
}

for s in "${SIZES[@]}"; do
  # Bookerly (Regular / Italic / Bold) — note spaces in source filenames
  bake bookerly "$s" regular "../builtinFonts/source/Bookerly/Bookerly.ttf"
  bake bookerly "$s" italic  "../builtinFonts/source/Bookerly/Bookerly Italic.ttf"
  bake bookerly "$s" bold    "../builtinFonts/source/Bookerly/Bookerly Bold.ttf"

  # Georgia (Regular / Italic / Bold)
  bake georgia "$s" regular "../builtinFonts/source/Georgia/Georgia-Regular.ttf"
  bake georgia "$s" italic  "../builtinFonts/source/Georgia/Georgia-Italic.ttf"
  bake georgia "$s" bold    "../builtinFonts/source/Georgia/Georgia-Bold.ttf"

  # Verdana (Regular / Italic / Bold)
  bake verdana "$s" regular "../builtinFonts/source/Verdana/Verdana-Regular.ttf"
  bake verdana "$s" italic  "../builtinFonts/source/Verdana/Verdana-Italic.ttf"
  bake verdana "$s" bold    "../builtinFonts/source/Verdana/Verdana-Bold.ttf"

  # Merriweather (Regular / Italic / Bold / BoldItalic)
  bake merriweather "$s" regular    "../builtinFonts/source/Merriweather/Merriweather-Regular.ttf"
  bake merriweather "$s" italic     "../builtinFonts/source/Merriweather/Merriweather-Italic.ttf"
  bake merriweather "$s" bold       "../builtinFonts/source/Merriweather/Merriweather-Bold.ttf"
  bake merriweather "$s" bolditalic "../builtinFonts/source/Merriweather/Merriweather-BoldItalic.ttf"
done

echo ""
echo "Running compression verification..."
python3 verify_compression.py ../builtinFonts/
