# Lector 0.55.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## New

- **Fill Pages.** A switch in Text Settings, off by default, kept per book like the rest of the layout. Lector normally moves a line to the next page so a paragraph is not split badly and a heading stays with its text, which leaves some pages a line or two short. With Fill Pages on, every page runs to its last line.
- **X4 Pro: Refresh Wallpaper.** A switch in Display settings, off by default. One minute after you lock, the device wakes by itself, draws the same wallpaper once more and sleeps again. This clears the gray left by the screen that was showing before. It only happens above 15% battery, once per lock.

## Fixed

- **Downloads from GitHub no longer stall.** Updates and font downloads could hang for 30 seconds and fail, because one very long line in the server's answer was read wrongly.
- **Unusual spaces** (no-break, thin, em and ideographic spaces) draw as a normal space instead of a missing-character box. Zero-width characters draw nothing.

## Changed

- X3 and X4: secure connections to servers with RSA certificates use a leaner code path for the key check. It costs about 17 KB of flash. The memory gain is not measured yet.
