#pragma once

#include <DeviceProfile.h>

class GfxRenderer;

// The clean pass that runs before a wallpaper sleep screen is drawn.
//
// A ghost is trapped charge from frames the panel drove earlier, and the only thing that
// reliably shifts it is driving every pixel to the opposite rail and back. The X3 gets that
// for free: its grayscale base calls requestResync() (HalDisplay.cpp:290), which clears the
// panel before the base pass lands. The X4 Pro's SSD1677 has no requestResync() at all, so
// its wallpaper is painted straight over whatever the reader left in the ink — and the
// pixels that survive most are the high-contrast ones that sat unchanged for many
// differential updates, which is exactly the status bar along the top.
//
// So the scrub is for the boards that cannot resync. Running it on the X3 too would spend a
// second of panel time re-solving a problem that board has already solved.
//
// Declared here rather than in SleepActivity so the device rule is a pure function over a
// DeviceProfile and can be host-tested without a panel. See test/sleep_face_paint.
inline constexpr bool sleepNeedsPreClear(const DeviceProfile& dev) {
  // isX3 rather than "is not X4 Pro": the deciding property is whether the driver implements
  // requestResync(), and among the boards here the X3 is the one that does.
  return !dev.isX3;
}

// Drives the panel black, then white, before the wallpaper is drawn. Returns what it cost in
// milliseconds, or 0 when this device does not need it.
//
// Black first, then white: white alone leaves white-on-white history untouched, so a ghost
// made of pale furniture would survive it.
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev);
