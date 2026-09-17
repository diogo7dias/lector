# lector 0.31.13

## Fixes

- **X4 Pro wallpapers show their grey tones again.** The sleep screen paints in
  three passes: a black-and-white base, then two grey planes nudged onto it. The
  X4 Pro was running that base at `FULL_REFRESH`, which parks the panel's pixels
  in a different charge state than the grey nudges are calibrated for, so both
  mid tones collapsed and the wallpaper came out as black, white and a single
  grey. The base is back on `HALF_REFRESH`, the same waveform the X3 uses.

  `FULL_REFRESH` had been set deliberately, to hide the previous frame ghosting
  under the wallpaper — the X4 Pro's controller cannot clear the panel before the
  base pass the way the X3 can. The tone loss is the worse of the two, so the
  ghosting is accepted for now.

  The C3 X4 is unchanged: it uses a different display controller with different
  grayscale tables, and this verdict does not carry over to it.
