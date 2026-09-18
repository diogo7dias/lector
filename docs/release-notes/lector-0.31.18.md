# lector 0.31.18

## Fixes

- **The X4 Pro's wallpapers keep their mid greys, and the status bar should stop
  ghosting.** One wrong line caused both.

  The black-and-white base pass under a grayscale sleep screen was running a FULL
  (GC) refresh on this device. The grey-nudge waveform that paints the wallpaper
  on top of it is calibrated against the lighter HALF base, so a FULL base parked
  the pixels in a different charge state and the two mid tones collapsed — the
  wallpaper came out black, white and a single grey.

  The same line explains the ghosting. The UC8279 driver only runs its real
  black-and-white activation, which is the panel's periodic ghost purge, when the
  base is HALF. A FULL base skipped that purge on every sleep, so the status bar
  kept its faint outline no matter how hard the pre-sleep scrub worked.

  The base is now HALF on every device, which is what the code's own comments had
  said all along.

## Removed

- **LUT Lab.** It was built to hunt for a better grayscale waveform, on the theory
  that the tables were at fault. They were not. Every variant it offered was
  judged worse than the stock waveform on hardware, and the stock waveform is what
  the fix above restores.
