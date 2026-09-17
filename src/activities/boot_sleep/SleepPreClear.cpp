#include "activities/boot_sleep/SleepPreClear.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev) {
  if (!sleepNeedsPreClear(dev)) return 0;

  const uint32_t startMs = millis();

  // Every pixel to ink, then every pixel to paper. Two FULL passes: the differential modes
  // would idle the pixels that already look right, which are precisely the ones holding the
  // ghost.
  renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), true);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  return millis() - startMs;
}
