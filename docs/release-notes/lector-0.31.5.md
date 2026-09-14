# Lector 0.31.5

## Unlock

The unlock is the headline. Pressing the button used to take about five seconds
to put a readable page on the glass; it now takes about one and a half.

The cost was a full clearing pass before the reader painted: a complete waveform
over a blank framebuffer, 710 ms on the X3 and 1809 ms on the X4 and X4 Pro,
followed by the page's own refresh. Two panel passes for one unlock. That pass
existed because a sleep face is arbitrary content and a short waveform cannot be
trusted to clear ink it does not know about.

It is gone. The reader's first paint now drives **every** pixel toward its target
regardless of what the panel held before, using the mode the drivers already use
for night mode. One pass carries the whole unlock, whatever the sleep screen was
showing: wallpaper, cover, or the plain fallback. Nothing is saved, decoded or
guessed about the previous screen.

Measured cost, button to page, on a cached book:

| Board | Before | After |
|---|---:|---:|
| X4 (SSD1677) | ~3.3 s | ~1.3 s |
| X3 (UC8253) | ~2.1 s | ~1.4 s |
| X4 Pro (SSD1677 and UC8179 batches) | ~3.3 s | ~1.3 s |

Two smaller cuts ride along: the 250 ms serial settle is no longer paid on the
X4's latch wake (every X4 unlock is a power-on reset, so the existing deep-sleep
skip never fired there), and the SD font family and book index now load while the
panel is busy rather than after it.

A single short waveform can leave a trace of dense ink. The first page turn after
a wake therefore runs a full clean pass, so any residue lives on the first page
only. **Settings > Display > Fast Unlock** (on by default) turns all of it off and
restores the old clearing pass, with no reflash, if a panel ghosts.

## Sleep screen

Three states remain, in fallback order:

1. **Wallpaper** — the default. `/sleep.bmp`, `/sleep.pxc`, or the rotation in
   `/sleep`.
2. **Book cover** — when there is no wallpaper, or when chosen directly.
3. **Lector** — a white page with the word centred, when there is neither.

The crest, the Stats Dashboard and the Transparent face are removed, along with
the boot logos behind them. Saved selections migrate to Wallpaper. The firmware
is 134 KB smaller on the C3 and 133 KB smaller on the S3.

## Flat interface

- Scrollbars are replaced by a single chevron above or below a list, shown only
  when there is something in that direction. Rows reclaim the track's width.
- One size and one weight everywhere: the button-hint face, regular. No bold in
  any menu, list, header, popup or settings row. Book text is untouched.
- Text wraps instead of being cut. Headers, help text, the path bar, popups and
  settings rows grow to fit their content.
- The in-book menu header sits flush at the top of the screen.
- Every board gets the same look, including the X4 Pro.

## Fonts

JetBrains Mono and Signika join the SD font families, sizes 10 to 18, both under
the SIL Open Font License 1.1. They appear in Manage Fonts once the matching
`lector-fonts` manifest release is published.

---

Validated with 1,231 passing host tests and clean builds of both targets. Device
checks still wanted: the look on every screen and orientation, and a wake from a
dark wallpaper into a text page on each panel batch.

The X3 and X4 use `firmware.bin`. The X4 Pro uses `firmware-x4pro.bin`.
