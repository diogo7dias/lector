# Reading

A reader opens an EPUB from the file browser, turns pages by swiping, holds a word for the
dictionary, taps the page centre for the in-book menu, and finds the same page after a
power cycle.

## Sub-features

- `read-open` a book opens from Browse Files into `EpubReader`.
- `read-swipe` a right-to-left swipe turns to the next page.
- `read-hold` holding a word shows the dictionary message (no dictionary on the sim card), which clears without turning the page.
- `read-menu` a tap on the page centre opens `EpubReaderMenu`.
- `read-resume` after a reboot the book reopens on the page it was left at.

## How to get to it (user POV)

- Home, Browse Files, tap a book.
- After a reboot, the last book reopens by itself.

## Driving it with lv

Preconditions: baseline; the card holds `test_kerning_ligature.epub`.

- **Open.** `lv tap 240 517` twice (Browse Files), then `lv tap 150 310` twice (first book).
  Poll `lv state` until `activity` is `EpubReader`; `lv shot page1`.
- **Swipe.** `lv swipe 420 420 100 420`. `frames` rises, `ink` above 5000, and the `lv shot page2`
  PNG differs from `page1.png` (`cmp` them).
- **Hold.** `lv tap 240 300 1200`. `activity` stays `EpubReader`; within 30 s the body bands
  (`lv ink 0 <y> 480 20` for y = 0, 20 ... 720) match those read before the hold.
- **Menu.** `lv tap 240 400`. `activity` becomes `EpubReaderMenu`. `lv press home` returns to `Home`.
- **Resume.** `lv reboot`, poll until `activity` is `EpubReader`, then poll the body bands
  until they equal the page-2 bands. `lv shot resumed`.

## Gotchas

- `EpubReader` is entered before the first page is painted: poll, never read once.
- The status bar's `+N` counts pages turned this session and resets on reboot: compare
  only y below 740.
- Page 1 of `test_kerning_ligature.epub` is its cover image (`ink` about 330 000); judge text on page 2.
- First open of a book builds its section cache on the card and is slower than later opens.
