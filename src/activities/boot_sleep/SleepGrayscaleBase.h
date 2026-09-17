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
inline constexpr HalDisplay::RefreshMode sleepGrayscaleBaseRefresh(const DeviceProfile& dev) {
  // The C3 X4 keeps FULL: nobody has judged it on hardware, and the X4 Pro verdict does not
  // transfer across controllers (UC8279 vs SSD1677 have different grayscale LUTs).
  const bool uc8279X4 = dev.controllerIsUc8279 && !dev.isX3;
  return uc8279X4 ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH;
}
