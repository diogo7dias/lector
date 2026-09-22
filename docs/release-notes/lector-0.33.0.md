# Lector 0.33.0

## Changed

- Lists move exactly one row per press. A quick second press no longer jumps ahead.
- Holding up or down scrolls every list, settings included: one row at a time, then five at a time after about a second, stopping at the end.
- Faster SD card reads, faster text drawing and quicker page turns with long chapter titles.
- Presses made while a list or page is redrawing are no longer lost.
- KOReader credentials and reader presets load when first needed, so waking is slightly faster.
- Existing section layout caches are rebuilt once after updating. Books and reading progress on the SD card are preserved.

## Fixed

- The first-line indent no longer reappears partway through very long paragraphs.
- A crash risk after the dictionary, OPDS or web server screens released SD font memory.
- A book whose own look turns on embedded styles now gets its CSS.
