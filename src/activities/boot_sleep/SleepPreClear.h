#pragma once

#include <DeviceProfile.h>

#include <cstdint>

class GfxRenderer;

// The clean pass that runs before a wallpaper sleep screen is drawn.
//
// A ghost is trapped charge from frames the panel drove earlier, and the only thing that
// reliably shifts it is driving every pixel to the opposite rail and back.
//
// This file used to say the X3 got that for free from the requestResync() its grayscale
// base asks for (HalDisplay.cpp:290), and so skipped the scrub there. That premise was
// wrong, and the X3 lock screen ghosted because of it. requestResync() is a SYNC, not a
// clear: it makes the next pass take displayStart()'s full-sync branch.
//
// That branch used to fill DTM1 with 0xFF and paint the target frame against that white
// baseline. With the baseline forced white, a pixel's transition cell depended only on where
// it was going, so only two of the four fired, and only one of them flashed:
//
//   target white -> WW -> lut_x3_ww_full: 24 frames to the black rail, then 24 back to
//                         white. A real flash.
//   target black -> WB -> lut_x3_wb_full: 28 idle frames, then 14 to black. Never visits
//                         white.
//
// The bank's excursion for pixels that end up black lives in lut_x3_bb_full (0x84: 24
// frames to white before 14 to black), and the forced white baseline is exactly what
// guaranteed BB never fired. The BW base under the two gray planes paints every level except
// pure white as black (DirectPixelWriter.h:152-155), so the whole mid-tone area of a
// wallpaper was the set of pixels that never reached the opposite rail.
//
// The full sync now writes DTM1 = the target (Uc8253X3Driver.cpp, displayStart), so every
// pixel is an unchanged cell and both WW and BB flash. A requestResync() base therefore
// discharges the dark part of the picture too. The scrub still runs here, because a pass
// that has to be right about whatever the panel held is cheaper to guarantee than to
// argue, and one white FULL is all the X3 needs of it.
//
// Hence: the scrub runs on every board. The X3 just needs far less of it, because its own
// full bank carries the black excursion inside a single white pass.
//
// Declared here rather than in SleepActivity so the device rule is a pure function over a
// DeviceProfile and can be host-tested without a panel. See test/sleep_face_paint.
inline constexpr uint8_t sleepPreClearSubmissions(const DeviceProfile& dev) {
  // X3: one all-white FULL. Every pixel classifies WW against the forced white baseline
  // and takes lut_x3_ww_full's black excursion, so one submission carries the whole panel
  // through both rails.
  //
  // Everything else: the frames have to be drawn, because no single pass on those boards
  // inverts a pixel that is not changing. Black then white, twice — see the cycle comment
  // in sleepPreClear().
  return dev.isX3 ? 1 : 4;
}

// Drives every pixel through both rails before the wallpaper is drawn. Returns what it
// cost in milliseconds.
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev);
