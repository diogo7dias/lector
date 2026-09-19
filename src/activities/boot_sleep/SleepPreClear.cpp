#include "activities/boot_sleep/SleepPreClear.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev) {
  const uint32_t startMs = millis();

  if (dev.isX3) {
    // Candidate scrub: all-white target against the full-sync white baseline.
    // One renderer call fires the full waveform AND the driver's FAST settle.
    // HalDisplay.cpp records prior FULL/HALF timings of 2016/2551 ms; this lock
    // sequence and its effect on ghosting have not been measured on hardware.
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
