# Lector 0.42.0

## Added

- Search inside a book from the reader menu.
- Status bar: **Chapter time left** and **Book time left**.
- Progress bar: chapter ticks.
- **Forward** after following a link and going back.
- Per-book text settings: **Kerning**, **Ligatures**, **Link Underline**, **Book Margins**, **Break Before Headings** (h1/h2 start a new page), **Word Expansion** (Off / Some / More: loose justified lines spread up to 1-2 px between letters).
- Display: **Text Contrast** (darker anti-aliased greys, up to Max) and a **BOLDER** step for Paperback Look.
- Refresh: **Night Refresh Frequency** and **Refresh at Chapter Start**.
- TXT: Windows-1252 files show accented letters correctly.

## Changed

- X4 Pro: fewer idle wake-ups (better battery); waking from deep sleep skips the display probe (~117 ms faster).
- KOReader sync: the result screen no longer keeps the CPU awake.
- Opening a book reads only the stylesheet cache header.

## Fixed

- **Embedded Layout Style** off no longer applies the book's margins and padding.
