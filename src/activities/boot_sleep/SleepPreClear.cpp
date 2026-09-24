#include "activities/boot_sleep/SleepPreClear.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

// cppcheck-suppress constParameterReference ; clearScreen/displayBuffer are non-const
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev) {
  const uint32_t startMs = millis();

  if (dev.isX3) {
    // One white FULL, and the panel is scrubbed. A FULL takes the driver's full-sync
    // branch, which forces the DTM1 baseline white, so an all-white target puts every
    // pixel in WW and lut_x3_ww_full drives 24 frames to black before 24 back to white.
    // Both rails, whole panel, one submission. Measured cost of an X3 FULL: 2016 ms
    // (HalDisplay.cpp:107-109), spent after the reader has put the device down.
    //
    // HALF would scrub the same way but through requestResync(), which adds a
    // conditioning pass and measures 2551 ms for the same picture. FULL is the cheaper
    // spelling of this pass on this board.
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return millis() - startMs;
  }

  // Two cycles, not one. A single black/white pass left the status bar still faintly
  // visible on an X4 Pro: that furniture holds the deepest charge because it sat
  // unchanged through the whole reading session, and one round trip does not fully
  // depolarise it. The second cycle is what the bench's strongest setting did.
  //
  // ponytail: two is what hardware needed, not a tuned optimum. If a ghost ever
  // survives this, the next lever is the waveform/LUT, not a third cycle.
  for (int cycle = 0; cycle < 2; cycle++) {
    // Every pixel to ink, then every pixel to paper. Two FULL passes: the differential
    // modes would idle the pixels that already look right, which are precisely the ones
    // holding the ghost.
    renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), true);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }

  return millis() - startMs;
}
