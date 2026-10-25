# Buttons on the C3 boards (X4, X3)

The X4 and X3 run one ESP32-C3 binary with no touch panel: a reader moves with the front
buttons (Back, Confirm, Left, Right) and the side buttons (Up, Down). The X3 ships with
two panel controllers; the boot probe must pick the right one.

## Sub-features

- `c3-browse` Confirm on Home opens the file browser, Down moves a row, Back returns Home.
- `c3-read` Up then Confirm in the file browser opens the last book; Right turns a page.
- `c3-panel` the X3 reports its controller: `UC8253`, or `UC8279` on the newer run.

## How to get to it (user POV)

- Switch the reader on and use the front and side buttons.

## Driving it with lv

Preconditions: `pio run -e default` built; a fresh `lv start x4` (or `x3`, `x3uc8279`) on `Home`.

- **Browse.** `lv press confirm`: `activity` becomes `FileBrowser`. `lv press down`: `frames` rises,
  still `FileBrowser`. `lv press back`: `activity` is `Home`.
- **Open.** `lv press confirm`, `lv press up`, `lv press confirm`. Poll `lv state` until `activity` is
  `EpubReader`, wait for frames to settle, `lv shot page1`.
- **Turn.** `lv press right`. `frames` rises, `ink` above 5000; `lv shot page2` differs from `page1.png` (`cmp`).
- **Panel.** `lv state` `controller` is `SSD1677` (x4), `UC8253` (x3) or `UC8279` (x3uc8279); on
  x3uc8279 `lv log promoted` prints `[XTDET] promoted UC8253 -> UC8279`.

## Gotchas

- Same `firmware.bin` for all three boards: build `default`, not a per-board env.
- `lv tap` and `lv swipe` fail with `POST ... failed` here (the server answers 400): no touch.
- Up from the top row of the file browser wraps to the last entry, which is how the recipe reaches a book.
- The first page of the test book is its cover image (`ink` about 330 000-350 000); the text check belongs on page 2.
