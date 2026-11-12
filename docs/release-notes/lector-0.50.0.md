# Lector 0.50.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## Fixed

- A character missing from an SD TrueType font shows the font's empty box instead of vanishing from the line.
- Random book picks and Nearby position sync no longer look inside the `System Volume Information` and `XTCache` folders. The file browser hides `XTCache` too.
- The button remap screen finishes when you let go of the last button, so that release no longer acts on the Settings screen under the new mapping.
- A hidden password ending in an accented letter shows that letter, not a broken symbol.
- A damaged SD font file (for example an interrupted copy) draws the damaged letters blank instead of garbage.
- Very long chapters (past about 2,000 pages or 500 links) no longer risk a restart from one large memory request while they are indexed.
- Hebrew: the four-letter divine name is shown as ה' in right-to-left text.
- A rare crash while the reader switched to low-power mode.

## Changed

- Less memory used: unused Arduino libraries and Bluetooth are left out of the firmware, a CJK fallback font loads once when a book's CSS asks for the same size again, and Chinese and Japanese words lay out with fewer allocations.
