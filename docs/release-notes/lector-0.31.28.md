# Lector 0.31.28

## Changed

- Removed the hyphenation feature and its setting. Text now keeps words intact at line endings instead of inserting dictionary-based hyphens.
- Existing section layout caches are rebuilt once after updating. Reading progress and books on the SD card are preserved.
- Soft hyphens already present in EPUB content continue to work.

## Fixed

- Removed the unused hyphenation data and layout path from the firmware, reducing the firmware size.
