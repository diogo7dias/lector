#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

# ChareInk, the built-in reading family: 12, 14 and 16 pt (BUILTIN_READER_POINT_SIZES).
CHAREINK_FONT_SIZES=(12 14 16)

for size in ${CHAREINK_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="chareink_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/ChareInk7/ChareInk7-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

# Paragraph numbers: Cozette's digits alone, 1-bit at 1x, 2x and 3x its native 13px grid
# (--dpi 72 so size means pixels), so every size is an exact pixel multiple.
for px in 13 26 39; do
  output_path="../builtinFonts/cozette_digits_${px}.h"
  python fontconvert.py cozette_digits_${px} $px ../builtinFonts/source/Cozette/Cozette-Regular.ttf \
    --dpi 72 --only-intervals --additional-intervals 0x30,0x39 > $output_path
  echo "Generated $output_path"
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
