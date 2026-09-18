# lector 0.31.16

## Temporary

- **LUT Lab**, a new row on the Home screen, for finding a better grayscale
  waveform for the X4 Pro's sleep screen by eye.

  The X4 Pro renders wallpapers through a four-level grayscale table that has
  never been calibrated against this panel — it takes the SDK's defaults, while
  every Lector-specific tuning is scoped to the X3 and the C3 X4. The result is
  weak, flat mid greys. The correct values are analog voltages that cannot be
  derived from source code, only judged on glass, so the lab makes them
  switchable on the device.

  Nine variants: an unchanged control, four VCOM steps, two VSH1 steps and two
  phase-timing probes. Each changes exactly one thing, so a verdict is
  attributable. The lab can also pin a wallpaper so the sleep screen stops
  rotating, which is what makes two variants comparable, and step through the
  other wallpapers to check a promising variant against several images. Every
  choice survives sleep, because locking and unlocking is the test.

  This screen is temporary and will be removed once a winner is found. It changes
  nothing on the X3 or the C3 X4.

## Fixes

- The pre-lock scrub from 0.31.15 is unchanged and still runs twice.
