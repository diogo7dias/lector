# Sleep and wake

Holding Power puts the reader to sleep: it draws the sleep screen, then enters deep sleep.
Power again wakes it back to where it was.

## Sub-features

- `sleep-screen` Power shows the `Sleep` activity.
- `sleep-deep` the device enters deep sleep afterwards.
- `sleep-wake` Power wakes it and leaves `Sleep`.

## How to get to it (user POV)

- Hold the Power button from any screen.

## Driving it with lv

Preconditions: baseline, any activity.

- **Sleep.** `lv press power`. Poll `lv state`: `activity` becomes `Sleep`, then `asleep` becomes `true` within 60 s. `lv shot sleep`.
- **Serial proof.** `lv log 'Entering deep sleep'` prints the line.
- **Wake.** `lv press power`. `asleep` becomes `false` within 120 s and `activity` is no longer `Sleep`. `lv shot woken`.

## Gotchas

- Emulated wake is slower than the device: `power` holds until a frame appears.
- Wake is a fresh boot: in the proof run it slept from `Settings` and woke into the last book (`EpubReader`). Never assert it returns to the screen held before sleep.
- The sleep screen is the last frame before deep sleep; take the shot before waking.
