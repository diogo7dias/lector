#pragma once

#include <HalFrontlight.h>

#include "CrossPointSettings.h"

// Push a just-changed frontlight setting down to the hardware, so the light answers
// the change instead of waiting for the next boot. Every writer of these fields calls
// it: the Settings rows and the web settings page. No-op for every other field, and
// on a board with no frontlight (HalFrontlight is inert there).
inline void applyFrontlightSetting(uint8_t CrossPointSettings::* const valuePtr) {
  if (valuePtr == &CrossPointSettings::frontlightOn) {
    Frontlight.setOn(SETTINGS.frontlightOn != 0);
  } else if (valuePtr == &CrossPointSettings::frontlightBrightness ||
             valuePtr == &CrossPointSettings::frontlightMaxBrightness) {
    SETTINGS.clampFrontlightBrightness();
    Frontlight.setBrightness(SETTINGS.frontlightBrightness);
  } else if (valuePtr == &CrossPointSettings::frontlightWarmth) {
    Frontlight.setWarmth(SETTINGS.frontlightWarmth);
  }
}
