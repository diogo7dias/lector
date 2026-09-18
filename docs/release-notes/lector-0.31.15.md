# lector 0.31.15

## Fixes

- **A stronger scrub before the lock screen.** The clean pass added in 0.31.14
  runs twice instead of once. A single black-and-white cycle left the status bar
  still faintly visible: that furniture holds the deepest charge, because it sat
  unchanged on the panel through a whole reading session, and one round trip does
  not fully depolarise it.

  Locking costs about a second more as a result. It happens after the device has
  been put down, so nobody waits for it.

  If a ghost still survives this, the next thing to change is the panel's
  grayscale waveform, not a third cycle.
