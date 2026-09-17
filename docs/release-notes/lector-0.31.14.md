# lector 0.31.14

## Fixes

- **The lock screen no longer ghosts the screen that came before it.** The panel
  is now driven fully black and then fully white before a wallpaper sleep screen
  is drawn, on every device whose display controller cannot clear itself.

  A ghost is trapped charge from earlier frames, and the pixels that hold it
  longest are the high-contrast ones that sat unchanged through many updates —
  which is why the status bar along the top was the most visible part of it. The
  X3 never had this problem: its grayscale base clears the panel first as part of
  its own refresh. The X4 Pro's controller has no equivalent, so its wallpaper
  was painted straight over whatever the reader left behind.

  This scrub already existed but was compiled into development builds only, so no
  release firmware has ever run it. It now ships for everyone.

  The cost is one extra clean pass at lock, which happens after the device has
  been put down rather than while anyone is waiting for it.
