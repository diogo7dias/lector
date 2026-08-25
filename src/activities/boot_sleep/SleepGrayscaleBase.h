#pragma once

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

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
inline HalDisplay::RefreshMode sleepGrayscaleBaseRefresh() {
  const bool uc8279X4 =
      BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 && !gpio.deviceIsX3();
  return uc8279X4 ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH;
}
