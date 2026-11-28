#pragma once

#include <HalFrontlight.h>

#include "CrossPointSettings.h"
#include "frontlight/FrontlightClock.h"

// Push a just-changed frontlight setting down to the hardware, so the light answers
// the change instead of waiting for the next boot. Every writer of these fields calls
// it: the Settings rows, the reader menu, and the web settings page. No-op for every
// other field, and on a board with no frontlight (HalFrontlight is inert there).
inline void applyFrontlightSetting(uint8_t CrossPointSettings::* const valuePtr) {
  if (valuePtr == &CrossPointSettings::frontlightOn) {
    frontlight::cancelCandleFade();
    Frontlight.setOn(SETTINGS.frontlightOn != 0);
    if (SETTINGS.frontlightOn != 0) frontlight::syncScheduledWarmth();
  } else if (valuePtr == &CrossPointSettings::frontlightBrightness ||
             valuePtr == &CrossPointSettings::frontlightMaxBrightness ||
             valuePtr == &CrossPointSettings::nightFrontlightBrightness) {
    SETTINGS.clampFrontlightBrightness();
    Frontlight.setBrightness(SETTINGS.activeFrontlightBrightness());
  } else if (valuePtr == &CrossPointSettings::frontlightWarmth ||
             valuePtr == &CrossPointSettings::nightFrontlightWarmth) {
    Frontlight.setWarmth(SETTINGS.activeFrontlightWarmth());
  } else if (valuePtr == &CrossPointSettings::screenInverted) {
    if (!Frontlight.present()) return;
    frontlight::cancelCandleFade();
    SETTINGS.clampFrontlightBrightness();
    Frontlight.setBrightness(SETTINGS.activeFrontlightBrightness());
    Frontlight.setWarmth(SETTINGS.activeFrontlightWarmth());
    frontlight::syncScheduledWarmth();
  } else if (valuePtr == &CrossPointSettings::frontlightScheduleEnabled ||
             valuePtr == &CrossPointSettings::frontlightScheduleOn ||
             valuePtr == &CrossPointSettings::frontlightScheduleOff) {
    if (!Frontlight.hasColorTemperature()) return;
    if (SETTINGS.frontlightScheduleEnabled == 0) {
      Frontlight.setWarmth(SETTINGS.activeFrontlightWarmth());
    } else {
      frontlight::syncScheduledWarmth();
    }
  }
}
