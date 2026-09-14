# Lector 0.31.5

## Flat UI

- Scrollbars are gone. Lists show an up chevron when rows sit above the window and a down chevron when rows sit below, drawn outside the row band, so rows get the track's width back. The wrapped home and file lists use the same chevrons instead of "N more" badges.
- One UI size and one weight. Every UI text slot uses the button-hint face (UI 10); nothing in the UI chrome is bold. The black header band and inverted selection apply on every board, so the X4 Pro looks the same as the keys-only boards.
- Wrap, never truncate. Header titles, sub-headers, help text, notes, footnotes, the path bar, the banner popup and the option popup wrap, and their bands grow to fit. Settings rows take content-driven heights.
- The in-book menu header sits flush at the top, wrapped, at the one UI size.

## Wake

- **Fast Unlock** (Display settings, new, on by default) shortens the recovery-chord wait at wake from 500 ms to 100 ms on the X3 and X4 button ladder. The X4 Pro stays at 20 ms. Nothing that reaches the panel changes. Turn it off if a wake ever lands in the recovery firmware picker by itself. With Wake Straight to Book the blank already overlaps that wait, so the saving shows only when the wake draws banners.
- The wake-stage record splits the route into the reader: `font` (SD font family loaded) and `book` (book index open) now appear in the timings overlay and log line. The record format bumped to WAK7; the first wake after the update shows no previous timings.
- The perf log settings header records `fastUnlock`.

Not shipped from the unlock work: the retained-frame baseline restore, the X3 base-push drop and the SSD1677 boot one-shot fix all targeted the Quick Resume frame, which 0.31.4 removed. On this wake path the panel takes a FULL blank before the reader's first paint, which already retires the driver's one-shot, so those changes would have done nothing.

## Fonts

- JetBrains Mono (sizes 10-18, OFL 1.1) and Signika (sizes 10-18, regular and bold, OFL 1.1) join the SD font families. They appear in Manage Fonts once the `lector-fonts` manifest release that carries them is published.

Validated with 1,228 passing host tests and clean C3 and X4 Pro builds. Device checks remain: the flat look on every screen and both orientations, recovery chord still reachable with Fast Unlock on, `settle=` on and off, and the `font`/`book` stages in the wake line.

The X3 and X4 use `firmware.bin`. The X4 Pro uses `firmware-x4pro.bin`.
