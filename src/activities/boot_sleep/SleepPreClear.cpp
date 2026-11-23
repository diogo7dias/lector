#include "activities/boot_sleep/SleepPreClear.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

// cppcheck-suppress constParameterReference ; clearScreen/displayBuffer are non-const
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile&) {
  const uint32_t startMs = millis();
  // Match CrossPoint: one white HALF_REFRESH cleans the previous screen before the sleep face.
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return millis() - startMs;
}
