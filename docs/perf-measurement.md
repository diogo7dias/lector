# Panel performance measurement, X3 and X4

Why this exists: the X3 and X4 are separate performance targets, and the cost ranking so
far is read from the drivers rather than measured. This is the reference for the build
that replaces the reading with numbers.

Nothing here is filled in by hand. The device records everything, including the settings
each run was taken under.

## Building it

```
pio run -e gh_release_perf
```

Identical to `gh_release` plus `-DPERF_LOG=1`. A separate environment rather than a flag
added to the release build, because PlatformIO's dependency finder preprocesses the
sources: with the flag undefined it cannot see the includes behind the `#if`, and the
recorder's dependencies never get linked.

Cost: about 4 KB of RAM for the record buffer. In `gh_release` every recorder call
compiles to an empty inline body, so the stable line carries none of it.

## Output

`/perf/x3-000.csv` or `/perf/x4-000.csv` on the SD card, a new numbered file per boot, so
a lock-and-wake cycle produces several files rather than overwriting one.

Three comment lines of context first, written by the device: firmware version, battery,
orientation, font family and size, sleep image quality, Wake Straight to Book, and the
open book. Then one row per refresh:

| Column | Meaning |
|---|---|
| `seq` | Refresh counter since boot, starting at 0 |
| `ms` | `millis()` when the refresh was requested |
| `screen` | The activity that asked, e.g. `EpubReader`, `EpubReaderMenu`, `Home` |
| `req` | Mode the firmware asked for: `FAST`, `HALF`, `FULL` |
| `run` | Mode actually used, after the refresh policy and the driver's own overrides |
| `total_us` | Request to return, in microseconds: the number a finger feels |
| `async_start_us` | On an async refresh, the part that returned before the panel finished. 0 on a blocking refresh |

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

Then copy everything under `/perf` off both cards.

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
