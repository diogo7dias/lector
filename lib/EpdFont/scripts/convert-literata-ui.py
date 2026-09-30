#!/usr/bin/env python3
"""Bake the Literata faces the contents look draws with into builtin fonts.

Each face is pinned from the variable font at its own pixel size, with the
optical-size axis set to that same size (what a browser does with
font-optical-sizing: auto), so the firmware draws the glyph shapes the mockup
was designed with. --dpi 72 makes the size argument mean pixels exactly.

The small-caps face remaps the lowercase cmap entries onto Literata's own smcp
glyphs, so lowercase text drawn with it comes out in real small capitals.

Usage: python convert-literata-ui.py   (writes ../builtinFonts/literata_ui_*.h)
"""
import subprocess
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "builtinFonts" / "source" / "Literata"
OUT = HERE.parent / "builtinFonts"
WORK = HERE / "instanced_fonts" / "LiterataUI"

ROMAN = SOURCE / "Literata[opsz,wght].ttf"
ITALIC = SOURCE / "Literata-Italic[opsz,wght].ttf"

# name, source, pixel size, weight, small caps
FACES = [
    ("literata_ui_20_regular", ROMAN, 20, 400, False),    # rows
    ("literata_ui_20_bold", ROMAN, 20, 700, False),       # the selected row
    ("literata_ui_26_semibold", ROMAN, 26, 600, False),   # section headings
    ("literata_ui_25_semibold", ROMAN, 25, 600, False),   # the book title
    ("literata_ui_16_semibold", ROMAN, 16, 600, False),   # the heading's numeral
    ("literata_ui_15_smallcaps", ROMAN, 15, 400, True),   # the chapter line
    ("literata_ui_19_italic", ITALIC, 19, 400, False),    # the author
    ("literata_ui_15_italic", ITALIC, 15, 400, False),    # the progress line
]


def small_caps(font: TTFont) -> None:
    """Point every cmap entry with an smcp substitute at that substitute."""
    gsub = font["GSUB"].table
    lookups = set()
    for record in gsub.FeatureList.FeatureRecord:
        if record.FeatureTag == "smcp":
            lookups.update(record.Feature.LookupListIndex)
    mapping = {}
    for index in sorted(lookups):
        lookup = gsub.LookupList.Lookup[index]
        for sub in lookup.SubTable:
            if lookup.LookupType == 7:  # extension
                sub = sub.ExtSubTable
            mapping.update(getattr(sub, "mapping", {}) or {})
    if not mapping:
        sys.exit("Literata has no smcp single substitutions")
    for table in font["cmap"].tables:
        if table.isUnicode():
            for cp, glyph in list(table.cmap.items()):
                if glyph in mapping:
                    table.cmap[cp] = mapping[glyph]


def main() -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    for name, source, px, weight, smcp in FACES:
        font = instantiateVariableFont(TTFont(source), {"opsz": px, "wght": weight})
        if smcp:
            small_caps(font)
        static = WORK / f"{name}.ttf"
        font.save(static)
        header = OUT / f"{name}.h"
        with header.open("w") as out:
            subprocess.run(
                [sys.executable, str(HERE / "fontconvert.py"), name, str(px), str(static),
                 "--2bit", "--compress", "--dpi", "72"],
                check=True, stdout=out)
        print(f"Generated {header.relative_to(HERE.parent)}")


if __name__ == "__main__":
    main()
