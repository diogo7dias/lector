# lector 0.31.17

## Fixes

- **The LUT Lab now appears, and now edits the waveform this panel actually
  uses.** In 0.31.16 the Home row was hidden on every real device and its
  variants patched a table the hardware never loads.

  X4 Pro units ship with two different display controllers. The firmware probes
  which one is present at boot, and these units report a UC8279 — but the lab was
  written for the SSD1677, both in the check that shows the menu row and in the
  bytes it edited. It was a no-op on the only device with the problem.

  The row now appears for any UC8279 X4-class device, and the variants edit the
  UC8279 grayscale tables that are genuinely written to the panel.

## Temporary

- **Eight LUT Lab variants**, sweeping the gray phase's frame count.

  The vendor itself ships two versions of this waveform, and they differ in
  exactly one byte: the number of frames the grey phase drives for. That byte is
  therefore the tuning axis with real evidence behind it. A longer grey phase
  drives the mid tones harder, which is what weak, flat greys are asking for.

  Variant 0 is the stock waveform as a control, variant 2 is the other vendor
  table byte-for-byte, and the rest step the frame count one at a time up to four
  times the stock value. Nothing else changes: no voltages, no polarity, no
  transition waveform, so a verdict points at one number.

  The lab still pins a wallpaper so the sleep screen stops rotating, still steps
  through the other wallpapers, and still remembers everything across sleep. It
  will be removed once a winner is found, and it changes nothing on the X3.
