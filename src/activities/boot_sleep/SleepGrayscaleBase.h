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
// The X4 and X3 keep HALF: their base is the exact waveform the gray-nudge LUT was
// calibrated against, and driving it harder would shift every tone in the image.
//
// Takes the device as a PARAMETER rather than reading the `gpio` / `BoardConfig::ACTIVE`
// globals, so it is a pure function and host-testable. See test/sleep_face_paint.
inline constexpr HalDisplay::RefreshMode sleepGrayscaleBaseRefresh(const DeviceProfile& dev) {
  // EXPERIMENT (not for merge): the X4 Pro's SSD1677 has no requestResync() path, so its
  // grayscale base never gets the clean slate HalDisplay.cpp:290 gives the X3. FULL is the
  // nearest available approximation. Judge on hardware: deeper blacks vs visible blinking.
  const bool uc8279X4 = dev.controllerIsUc8279 && !dev.isX3;
  if (dev.isX4Pro) return HalDisplay::FULL_REFRESH;
  return uc8279X4 ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH;
}
