# Panel performance measurement, X3 and X4

Purpose: replace source-reading guesses with real numbers, so speed work is aimed
at whatever actually costs the most on each device.

The X3 and X4 are separate performance targets. The same firmware pays very
different costs on each panel, so every run below is done twice, once per device,
and the results are never averaged together.

The device does the recording. The firmware appends one line per refresh to a
file on the SD card. Nothing is read off the screen and nothing is typed by hand.

---

## What the instrumented build records

One CSV line per refresh, appended to `/perf-<device>.csv` on the SD card.

| Column | Meaning |
|---|---|
| `seq` | Refresh counter since boot, starting at 0 |
| `ms` | `millis()` when the refresh was requested |
| `screen` | Which screen asked: `reader`, `menu`, `home`, `boot`, `sleep`, `settings` |
| `req` | Mode the firmware asked for: `FAST`, `HALF`, `FULL` |
| `run` | Mode the driver actually ran, after policy and driver overrides |
| `bytes` | Bytes pushed over the panel SPI bus for this refresh |
| `bus_us` | Microseconds spent pushing those bytes |
| `wave_us` | Microseconds waiting for the panel BUSY line |
| `post_us` | Microseconds of post-work after the waveform (plane resync, settle pass, fixed delays) |
| `total_us` | Request to return, the number a finger actually feels |

`req` and `run` are recorded separately on purpose. Both drivers override the
requested mode in places, and the size of that gap is itself a finding.

## Why these columns

They split one refresh into the four things that can be attacked independently:

- `bus_us` is fixed by SPI clock and bytes. Attack with a faster clock or fewer bytes.
- `wave_us` is the ink. Only a different waveform changes it.
- `post_us` is work done after the picture is already correct. It is the best
  candidate for deferring off the critical path.
- `total_us` minus the other three is overhead worth explaining.

---

## Runs

Same book, same font, same font size, same orientation on both devices. Note the
book and settings in the run sheet so a later run can be compared to this one.

Do each run on the X3, then repeat the identical run on the X4.

### Run 1 — page turns, steady reading

Open a book mid-chapter. Turn forward 30 pages at a comfortable reading pace, a
few seconds between presses. Do not open any menu.

This is the number that matters most. It is the firmware's hot path.

### Run 2 — page turns, fast

Same book, same starting page. Turn forward 30 pages as fast as the device will
accept presses.

Run 1 versus Run 2 shows whether presses are being lost or queued while the panel
is busy, and how much the refresh policy's periodic clean pass costs when it
lands in a burst.

### Run 3 — menu open and close

From the reading page: open the in-book menu, move down three rows, open the
Sleep tab, close the menu back to the page. Repeat five times.

This path uses non-fast refreshes, so it is where the X3's fixed post-refresh
delay should show up plainly.

### Run 4 — lock and unlock

With `Settings, Display, Wake Straight to Book` ON: lock the device, wait ten
seconds, wake it with the power button, wait for the book. Repeat five times.

Then set the same setting OFF and repeat five times.

Wake is a special case: both drivers override the requested mode on the first
paint after `begin()`, so this run shows what the panel really does at unlock
rather than what the firmware asked for.

### Run 5 — cold boot

Power the device fully off, then boot it to the reading page. Once per device.

Establishes the fixed startup cost that no page-turn work can improve.

---

## Run sheet

Fill this in once per device. Everything else comes from the CSV.

| Field | X3 | X4 |
|---|---|---|
| Firmware version | | |
| Book file name | | |
| Font family and size | | |
| Orientation | | |
| Sleep Image Quality | | |
| Wake Straight to Book | | |
| Battery percent at start | | |
| Room temperature, roughly | | |
| Anything that looked wrong | | |

Temperature is on the list because e-ink waveform timing is temperature
compensated. A run in a cold room is not comparable to a warm one.

---

## Returning the results

Copy these off the SD card and hand them back:

- `/perf-x3.csv`
- `/perf-x4.csv`
- This file with the run sheet filled in

No screenshots and no transcription. If a run went wrong, say which `seq` range
to ignore rather than deleting lines.

---

## Known before measuring

Recorded here so the numbers can confirm or refute them, rather than being read
to fit them. All of these are from source, none from a stopwatch.

- X3 panel bus default is 16 MHz, X4 is 40 MHz.
- The X3 sends the whole frame twice per refresh: once to draw, once to resync
  the differential baseline.
- The X3 blocks for a fixed 200 ms after every non-fast refresh.
- After a full sync the X3 runs an extra settle pass, so one full refresh is
  really two.
- Asking for HALF on the X3 forces a full sync plus a conditioning pass, so it
  costs more than asking for FULL, not less.
- The X4 driver can write a sub-region; the X3 driver cannot.
- On a wake neither driver honours the requested mode.

If the CSV contradicts any line above, the CSV wins and this list gets corrected.
