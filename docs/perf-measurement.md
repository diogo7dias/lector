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

`FAST/HALF think 41 panel 623 split 96 prom 3 pll 09`

- `req/run` — mode asked for, and mode the driver actually ran.
- `think` — milliseconds from the button press to the refresh call. This is the firmware
  deciding what to draw. A dash means no press was outstanding, i.e. this paint was not
  answering a button.
- `panel` — milliseconds the refresh call itself took.
- `split` — on an async refresh, the part that returned before the panel had finished.
- `prom` — how many FAST requests the anti-ghost policy has promoted to something slower
  since boot.
- `pll` — the X3 frame-clock byte in force (see below). Meaningless on X4, printed anyway
  so one format serves both devices.

`think` against `panel` is the point of the overlay. A menu that feels slow is either the
firmware being slow to decide or the ink being slow to move, and those are unrelated
problems with unrelated fixes.

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
| `async_start_us` | On an async refresh, the part that returned before the panel finished. 0 on a blocking refresh |
| `think_ms` | Milliseconds from the button press that caused this paint. Empty when no press was outstanding |

`req` and `run` are separate columns on purpose. Both panel drivers override the requested
mode in places, and the size of that gap is itself one of the things being measured.

## What it cannot see, and why

The bus / waveform / post-work split lives inside the SDK, and the SDK is a submodule this
build does not fork for a measurement.

`async_start_us` is the usable stand-in. On async paths it is the time to fire the
waveform — commands issued, bytes pushed — while `total_us` also covers the wait and the
driver's post-work. The difference separates "pushing bytes" from "waiting for ink and
cleaning up", which is the distinction the speed work turns on.

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
