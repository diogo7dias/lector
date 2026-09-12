# Lector 0.31.3

A maintenance release for X3, X4, and X4 Pro.

## Faster lock and wake

- Locking performs one final state persistence instead of repeated SD/JSON writes.
- Quick Resume removes stale sleep frames without a redundant existence probe.
- Wake frame loading opens the frame directly, reducing SD directory lookups.
- Intermediate wallpaper and sleep-state writes were removed; the final persisted state is unchanged.

## Touch and compatibility fixes

- Improved touch press/release ownership across reader, keyboard, file browser, OPDS, and hint-band interactions.
- Added selected upstream fixes for Hangul NFD filenames, hidden HTML attributes, web-server paths, KOSync progress/compare handling, and X3 EPUB anti-aliasing.

The X3 and X4 use `firmware.bin`. The X4 Pro uses `firmware-x4pro.bin`.
