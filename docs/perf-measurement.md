# Panel performance measurement, X3 and X4

Why this exists: the X3 and X4 are separate performance targets, and the cost ranking so
far is read from the drivers rather than measured. This is the reference for the build
that replaces the reading with numbers.

Nothing here is filled in by hand. The device records everything, including the settings
each run was taken under.

## Switching it on

There is no measurement build any more. Any build can be measured: turn on
**Settings > System > Performance Timings**.

That single switch turns on three things at once:

- one CSV row per panel refresh, written to `/perf` on the SD card (below),
- a one-line overlay in the top-left corner of every frame, showing the previous
  refresh's cost while the device is in use,
- the wake-stage breakdown, which replaces the unlock banner footer and is also written
  into the CSV.

Off, it costs nothing at all: no file is opened, `WakeTiming` performs no SD write per
wake, and every refresh's `record()` call returns on its first line. On, the cost is about
4 KB of RAM for the record buffer plus a card write per sixteen refreshes.

The reason it is a setting rather than a build flag: numbers are only worth having if the
device that has the problem can produce them, and that device is running a release build.
A measurement that needs its own firmware is a measurement nobody takes.

## The overlay

`FAST/HALF t41 p623 w96 v500 s96 ink302/3800 pr3 pll09`

Abbreviated because it is one line across the top of the panel and the narrow axis is
480 px on X4. A line that does not fit is drawn off the edge, which is its own bug. The
CSV on the card carries the same fields with full names; that is the record, this is the
glance.

- `FAST/HALF` — mode asked for, and mode the driver actually ran.
- `t` — think: milliseconds from the button press to the refresh call. This is the
  firmware deciding what to draw. A dash means no press was outstanding, i.e. this paint
  was not answering a button.
- `p` — panel: milliseconds the refresh call itself took.
- `w` / `v` — of that panel time, wire and waveform. Wire is time inside SPI transactions,
  streaming the frame into controller RAM. Waveform is time waiting on BUSY while the
  panel drives. Whatever `p` does not account for is host work.
- `s` — on an async refresh, the part that returned before the panel had finished.
  Omitted when zero.
- `ink` — this frame's score out of 1000, then the ink debt outstanding. A text page turn
  scores around 300, a menu row move around 10, a whole-screen inversion 1000.
- `pr` — how many FAST requests the anti-ghost policy has promoted to something slower
  since boot.
- `pll` — the X3 frame-clock byte in force (see below). Meaningless on X4, printed anyway
  so one format serves both devices.

Two comparisons are the point of the overlay. `t` against `p` says whether a slow
interaction is the firmware deciding or the ink moving; those are unrelated problems with
unrelated fixes. `w` against `v` then says which half of the panel time is worth attacking
— a faster bus and fewer bytes fix one, only a different waveform fixes the other.

The line describes the PREVIOUS refresh, necessarily: a refresh's cost is not known until
the panel has finished, and by then the frame that would report it is already ink.

## Trying an X3 PLL byte

Write the value into `/perf/pll.txt` on the card and power-cycle — `0x19`, `19` and `25`
are all read as the same byte. No rebuild and no flash per candidate. The value in force
is printed in the overlay and in the CSV header, so a run cannot be attributed to the
wrong candidate. With no such file the driver's stock `0x09` is used.

Before landing a value as the default, all four must hold: FAST panel time falls at least
15%; body text is clean after 30 consecutive FAST passes; grayscale still shows four
distinct levels (check on a cover sleep screen, not on text — one PLL register scales
every waveform bank, including the one-frame grayscale phases); and HALF and FULL still
fully clear.

## Judging Fast Page Turns (X4)

Settings > System > Fast Page Turns picks the panel's cheap partial waveform for FAST
refreshes. On by default, and only on the X4: the X3's UC8253 has no second fast path,
and the Seeed Sticky and the X4 Pro keep their own configs, where `0x22 = 0x1C` does not
select the partial waveform at all and would run the full one on every page.

The trade is ghosting for speed, so it needs eyes rather than numbers. Before trusting it:

1. Read 200 or more consecutive pages of a text-heavy book, then inspect the page in
   raking light for residue of earlier pages.
2. Browse a cover-heavy folder, then open a book and check the text page underneath.
3. Repeat both with the setting off, for comparison on the same panel and the same book.

Residue that the next anti-ghost clean does not remove means the setting is wrong for that
panel. Two levers, in order: raise `DisplayRefreshPolicy::TURBO_DEBT_MULTIPLIER` so cheap
passes buy cleans faster, or lower `TURBO_RELOAD_EVERY` so the panel re-reads its
temperature more often. Both are in `lib/hal/DisplayRefreshPolicy.h`, with the arithmetic
for each choice written out beside them. Shipping the setting off by default is the
fallback if neither is enough.

The `turbo` column says what each pass actually ran, which is not the same as what was
asked for: a promoted pass, an inverted-content pass, and every eighth pass all run the
standard sequence regardless of the setting.

## Output

`/perf/x3-000.csv` or `/perf/x4-000.csv` on the SD card, a new numbered file per boot, so
a lock-and-wake cycle produces several files rather than overwriting one.

Three comment lines of context first, written by the device: firmware version, battery,
X3 PLL byte, orientation, font family and size, sleep image quality, Wake Straight to
Book, refresh frequency, and the open book. Then the previous wake's stage breakdown, then
one row per refresh:

| Column | Meaning |
|---|---|
| `seq` | Refresh counter since boot, starting at 0 |
| `ms` | `millis()` when the refresh was requested |
| `screen` | The activity that asked, e.g. `EpubReader`, `EpubReaderMenu`, `Home` |
| `req` | Mode the firmware asked for: `FAST`, `HALF`, `FULL` |
| `run` | Mode actually used, after the refresh policy and the driver's own overrides |
| `total_us` | Request to return, in microseconds: the number a finger feels |
| `wire_us` | Of that, microseconds inside SPI transactions — streaming the frame into controller RAM |
| `wave_us` | Of that, microseconds waiting on BUSY while the panel drives its waveform |
| `async_start_us` | On an async refresh, the part that returned before the panel finished. 0 on a blocking refresh |
| `think_ms` | Milliseconds from the button press that caused this paint. Empty when no press was outstanding |
| `ink` | What this frame was scored at, 0-1000 (see `lib/hal/FrameInkMetrics.h`). 0 on paths with no framebuffer to measure |
| `debt` | The anti-ghost ink debt after this pass. Crossing a threshold is what forces a clean |
| `turbo` | 1 when this pass ran the panel's cheap partial waveform (Fast Page Turns), 0 otherwise |

`req` and `run` are separate columns on purpose. Both panel drivers override the requested
mode in places, and the size of that gap is itself one of the things being measured.

`total_us - wire_us - wave_us` is host work: policy, ink scoring, and the driver's own
per-refresh bookkeeping. Each of the three has a different fix, and a single elapsed total
cannot tell them apart.

What the split found, measured on exp.31 over 508 refreshes:

| | total | wire | waveform | host |
|---|---|---|---|---|
| X4 page turn (n=340) | 578 ms | 67 ms | 505 ms | 5 ms |
| X3 page turn (n=65) | 623 ms | 51 ms | 567 ms | 5 ms |

The waveform is 87 to 91 percent of a refresh on both panels, so bus-side work is not
worth optimising: X4's 67 ms is exactly the three 48000-byte plane writes at 20 MHz, and
removing the two post-refresh ones would win about 38 ms of 578. The X4's 505 ms is the
vendor's absolute partial sequence (`0x22 = 0xFC`), which reloads panel temperature every
refresh; the incremental `0x22 = 0x1C` path costs about 77 ms and is what the Fast Page
Turns setting selects.

The sleep screen's 227 ms X4 "FAST" is not a faster page turn and should not be read as
one. Its `wire_us` is 1.5 ms and its `ink` is 500, which together identify it as the
grayscale base activation running the custom LUT — a different operation that never
drives a black-and-white page.

The end of each file carries the same split summed over the whole session, on a
`# split wire N ms wave N ms host N ms of N ms` line.

## What it cannot see, and why

Panel temperature is not recorded because neither driver can read one back. The
temperature values in the drivers are constants written to the panel, not readings.

## Runs

Five sequences, done on each device. The book does not have to match between them: a
refresh drives the whole panel whatever is on it, so page content barely touches the
number being measured.

1. **Steady reading.** Thirty page turns at reading pace. The hot path, and the number
   that matters most.
2. **Fast reading.** Thirty page turns as fast as the device accepts presses. Against
   run 1 this shows whether presses are lost while the panel is busy, and what the
   periodic clean pass costs inside a burst.
3. **Menu.** Open the in-book menu, move a few rows, switch tab, close. Five times. This
   path uses non-fast refreshes, where the X3's fixed post-refresh delay should show.
4. **Lock and unlock.** Five times with Wake Straight to Book on, five with it off. Both
   drivers override the requested mode on the first paint after `begin()`, so this shows
   what the panel really does at unlock.
5. **Cold boot.** Once. The fixed startup cost no page-turn work can improve.

Then copy everything under `/perf` off both cards. That directory is the whole hand-off:
it carries the wake breakdown, every refresh, and a per-mode summary written on the way
into sleep. Nothing has to be photographed off the screen.

## Known before measuring

Written down first so the numbers can refute them, rather than being read to fit them.
All from source, none from a stopwatch.

- X3 panel bus default is 16 MHz, X4 is 40 MHz.
- The X3 sends the whole frame twice per refresh: once to draw, once to resync the
  differential baseline.
- The X3 blocks for a fixed 200 ms after every non-fast refresh.
- After a full sync the X3 runs an extra settle pass, so one full refresh is really two.
- Asking for HALF on the X3 forces a full sync plus a conditioning pass, so it costs more
  than asking for FULL, not less.
- The X4 driver can write a sub-region; the X3 driver cannot.
- On a wake neither driver honours the requested mode.

If the CSV contradicts any line above, the CSV wins and this list gets corrected.
