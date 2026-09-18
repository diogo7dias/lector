#pragma once

#include <DeviceProfile.h>
#include <HalDisplay.h>

// Which refresh the black-and-white base pass of a grayscale sleep screen runs at.
//
// Every mode other than Full takes the UC8279's differential DU branch (see
// Uc8279X4Driver::displayStart): a light, fast waveform that leaves black as a dark grey.
// That is the right trade for a page turn and the wrong one for the picture the device
// wears while it is off — the wallpaper is the last thing drawn before sleep, so the
// second the full flash costs is spent after the reader has already put it down, and the
// two grey levels the AA overlay adds afterwards are only ever as deep as the base under
// them.
//
// The X4 Pro, X4 and X3 all keep HALF: their base is the exact waveform the gray-nudge LUT
// was calibrated against, and driving it harder shifts every tone in the image. A FULL (GC)
// base parks pixels in a different charge state, the differential nudge then lands unevenly,
// and the two mid greys collapse — the wallpaper comes out black, white and one grey.
//
// Takes the device as a PARAMETER rather than reading the `gpio` / `BoardConfig::ACTIVE`
// globals, so it is a pure function and host-testable. See test/sleep_face_paint.
// HALF everywhere. The X4 Pro probes as a UC8279 (not an SSD1677 as once assumed), so the
// UC8279-X4 branch that returned FULL was the branch this device actually took: the mid
// greys collapsed exactly as described above, and Uc8279X4Driver::displayGrayscaleBase only
// runs its real B/W activation — the periodic ghost purge — on the Half branch, so a FULL
// base skipped the purge on every sleep and left the status bar ghosted.
inline constexpr HalDisplay::RefreshMode sleepGrayscaleBaseRefresh(const DeviceProfile&) {
  return HalDisplay::HALF_REFRESH;
}
