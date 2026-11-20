# Library

Home > Library lists every book on the card by title, author under it. Picking a book opens it.
The list comes from an on-card index (`/.crosspoint/lector-library.idx`) that is rebuilt on the
first visit after boot and after any book was added, moved, renamed or deleted. Opening a book
files it into `/recents`, finishing one into `/read`; the next visit must show it once, at the new
path.

## Sub-features

- `lib-list` Library opens and lists the card's books, sorted by title, none twice.
- `lib-open` a row opens its book in `EpubReader`.
- `lib-follow` after a book was filed into `/recents`, the next visit lists the same books and
  the row opens the book from `/recents/...`.

## How to get to it (user POV)

- Home, Library.

## Driving it with lv

Preconditions: baseline (fresh `lv start x4pro`); the card holds the two test books.

- **Open.** `lv tap 240 570` twice (Library). `activity` becomes `Library`. `lv log LIBIDX` shows
  `built: 2 books (0 reused, 2 parsed ...)`. `lv shot lib-list`: two rows, "JPEG Image Tests" then
  "Kerning & Ligature Edge Cases" with "Crosspoint Test Fixtures" under it, `1/2 books` top right.
- **Open a book.** `lv tap 240 133` twice (the second row). `activity` becomes `EpubReader`;
  `lv log "LIB\]"` shows `open /test_kerning_ligature.epub`.
- **Follow.** `lv press home`; `lv log Filing` shows the move into `/recents/`. `lv tap 240 570` twice
  again: `lv log LIBIDX` shows a second `built: 2 books (1 reused, 1 parsed ...)`, and the list is
  the same two rows. Tapping the row opens the book from `/recents/`.
- Per-row text is logged at debug level only: x4 (`default` env) shows `row N: title | author` lines,
  x4pro does not. `sim/tests/x4/z-library.e2e.ts` uses them to prove "once each".

## Gotchas

- Rows tap twice on x4pro (arm, then open). Row y depends on how many lines the rows above have.
- The index is rebuilt in a few ms on this card; a card of hundreds of EPUBs takes seconds, longer
  the first time. Not modelled: SD speed, so no timing claim from the sim.
- /read filing (a finished book) is covered by the host test, `test/library_index`, not here:
  reaching the last page of a test book by turning pages is slow.
