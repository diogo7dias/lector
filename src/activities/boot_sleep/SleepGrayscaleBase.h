#pragma once

#include <DeviceProfile.h>
#include <HalDisplay.h>

// Which refresh the black-and-white base pass of a grayscale sleep screen runs at: HALF
// on every board.
//
// The base is the exact waveform the gray-nudge LUT was calibrated against. A FULL (GC)
// base parks pixels in a different charge state, the differential nudge then lands
// unevenly, and the two mid greys collapse: the wallpaper comes out black, white and one
// grey. The X4 Pro (a UC8279) once took FULL and showed exactly that, and since
// Uc8279X4Driver::displayGrayscaleBase runs its periodic ghost purge only on the Half
// branch, it also skipped the purge on every sleep and left the status bar ghosted.
//
// The device parameter is unused today; it stays so a board that needs a different base
// can get one without touching the callers (and so the function stays pure and
// host-testable, see test/sleep_face_paint).
inline constexpr HalDisplay::RefreshMode sleepGrayscaleBaseRefresh(const DeviceProfile&) {
  return HalDisplay::HALF_REFRESH;
}
