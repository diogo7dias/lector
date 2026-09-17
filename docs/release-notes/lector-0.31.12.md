# lector 0.31.12

Unlock is faster, and it always lands in a book.

## Faster unlock

The wake used to list every font family on the SD card before loading the one
font it needed. With 31 families installed that cost about 250 ms of every
unlock. It now resolves the family it wants directly and scans the whole card
only when something actually needs the full list — the font picker in settings,
or a font uploaded over WiFi.

Measured on an X4 Pro with a warm cache: button press to readable page went from
about 1.83 s to about 1.55 s, and the wake's display stage from 590 ms to
336 ms. The saving scales with how many font families are on the card, so a card
with a handful of families saves proportionally less. All three devices take the
same path; nothing here is board-specific.

## Unlock always opens a book

**Open Book on Boot** loses its **Off** choice and now defaults to **Last Book**.
A settings file still holding Off moves to Last Book on first boot. **Random
Book** is unchanged. Holding Back during boot still lands on Home, and a reader
that crashed on the previous boot still falls back to Home, so a bad book cannot
wedge the device.

**Wake Straight to Book** is no longer a setting. It was on by default, it is
what makes the fast wake path reachable, and turning it off only bought a
progress indication during a wake that is now too short to need one.

## Fixes

- A book using its own font (different from the global selection) could fall back
  to the built-in font after the change above. Every requested family is now
  resolved, not just the first one.
- The perf and trace diagnostic logs no longer probe every previous session file
  on the card to pick a filename.
