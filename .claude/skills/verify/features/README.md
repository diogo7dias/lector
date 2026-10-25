# lector verification map

The maintained source for verifying what a reader sees and does on lector. Read this index,
then use the matching feature file as the recipe. Coordinates are x4pro, upright 480x800.

Proven end to end through `lv` on 2026-10-25 (every recipe, every board).

## Baseline preconditions

- `pio run -e x4pro` built from the checkout under test; `lv doctor` reports healthy.
- A fresh `lv start x4pro`: empty flash (default settings), card with
  `test_kerning_ligature.epub` and `test_jpeg_images.epub`, device on `Home`.
- Never drive an instance this run did not start.

## Driving conventions

- `lv` means `.claude/skills/verify/bin/lv`, run from the repo root.
- Touch rows take two taps on x4pro: the first arms (dashed outline), the second opens.
- The Home key (`lv press home`) returns to `Home` from anywhere: use it to reset between recipes.
- Wait on `lv state` (activity, frames), never on a fixed sleep.

## Proof and skip reporting

- `lv shot` before and after each action under test; quote the `activity` each shows.
- Persistence proof always crosses `lv reboot`.
- Report a path the sim cannot model (Wi-Fi, frontlight, ghosting) as "needs device test",
  never as verified through a different path.

## Features

- [Home and navigation](./home.md): rows, arm-then-open, Home key, tap timing.
- [Reading](./reading.md): open a book, page turns, word hold, in-book menu, resume after reboot.
- [Settings](./settings.md): change a setting by touch, survives reboot.
- [Sleep and wake](./sleep.md): Power sleeps to the sleep screen and deep sleep, Power wakes.
- [Buttons on the C3 boards](./buttons-c3.md): x4, x3, x3uc8279 browse and read by key, panel controller check.
