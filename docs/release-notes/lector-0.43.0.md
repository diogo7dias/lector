# Lector 0.43.0

## Added

- **Clock Time Zone**: pick a city (Lisbon/London, Paris/Berlin/Madrid, Athens, New York, Chicago, Denver, Los Angeles, Sydney, Auckland) and the clock follows summer time by itself. **Manual (UTC offset)** stays the default.
- **Libron** in the downloadable fonts.
- Reading themes now save the status bar too: which items show, where they sit, and whether it is on. Themes saved before keep the book's current bar.
- Following a link and then sleeping or leaving reopens on the page you were viewing; Back still returns to where you tapped the link.

## Changed

- Wider side margins: list rows, Settings grid rows, the status bar and the header now start 20 px from the edge.
- Keyboard: long tips wrap onto two lines instead of being cut off, and nothing covers the number row.
- Kerning: fonts with both kerning tables no longer stack pairs (small UI font regenerated).
- X4 Pro: folders list up to 8000 names; link taps resolve faster; crash reports carry a backtrace.

## Fixed

- A book that cannot be laid out is no longer reopened on every wake.
- Dictionaries in dictzip format read reliably.
- Web uploads from a Mac no longer create a second, identical-looking file.
- TLS seeds its random generator from the hardware RNG.
- KOReader sync: positions inside headings resolve to the heading.
- A failed SD read in an uncompressed EPUB entry stops instead of reading past the buffer.
- Font uploads, cover images and section caches are written safely: a reset never leaves a half-written file.
- Sleep from under the light panel shows the sleep screen; Settings save on every way out; a slow drag scrolls lists; a failed nearby send retries.
