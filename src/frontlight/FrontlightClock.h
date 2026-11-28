#pragma once

#include <HalClock.h>
#include <HalFrontlight.h>
#include <Logging.h>

#include <cstdint>

#include "CrossPointSettings.h"
#include "frontlight/FrontlightBootPolicy.h"

// Runtime state for the X4 Pro clock-following warmth curve and the Quick Panel
// Candle Fade timer. Zero background tasks and zero wakeups: warmth updates
// lazily on page turns, and Candle Fade steps only while the device is awake.
namespace frontlight {

namespace detail {
struct CandleState {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t lastTickMs = 0;
  uint8_t startBrightness = 0;
  uint8_t startWarmth = 0;
};

inline CandleState& candleState() {
  static CandleState state;
  return state;
}
}  // namespace detail

inline bool candleFadeActive() { return detail::candleState().active; }

inline void cancelCandleFade() {
  auto& s = detail::candleState();
  if (!s.active) return;
  s.active = false;
  LOG_DBG("FL", "Candle Fade cancelled");
}

inline uint8_t candleFadeRemainingMinutes(const uint32_t nowMs) {
  const auto& s = detail::candleState();
  if (!s.active) return 0;
  const uint32_t elapsed = nowMs - s.startMs;
  if (elapsed >= CANDLE_FADE_DURATION_MS) return 0;
  const uint32_t remMs = CANDLE_FADE_DURATION_MS - elapsed;
  return static_cast<uint8_t>((remMs + 59999UL) / 60000UL);
}

inline bool toggleCandleFade(const uint32_t nowMs) {
  if (!Frontlight.hasColorTemperature()) return false;
  auto& s = detail::candleState();
  if (s.active) {
    s.active = false;
    LOG_INF("FL", "Candle Fade off");
    return true;
  }
  SETTINGS.clampFrontlightBrightness();
  if (!Frontlight.isOn() || Frontlight.brightness() == 0) {
    const uint8_t b = SETTINGS.activeFrontlightBrightness() > 0 ? SETTINGS.activeFrontlightBrightness()
                                                                : SETTINGS.frontlightMaxBrightness;
    Frontlight.setOn(true);
    Frontlight.setBrightness(b);
    Frontlight.setWarmth(SETTINGS.activeFrontlightWarmth());
  }
  s.startBrightness = Frontlight.brightness();
  s.startWarmth = Frontlight.warmth();
  s.startMs = nowMs;
  s.lastTickMs = nowMs;
  s.active = true;
  LOG_INF("FL", "Candle Fade started (%u min from b=%u w=%u)", static_cast<unsigned>(CANDLE_FADE_DURATION_MS / 60000UL),
          s.startBrightness, s.startWarmth);
  return true;
}

inline void tickCandleFade(const uint32_t nowMs) {
  auto& s = detail::candleState();
  if (!s.active) return;
  const uint32_t elapsed = nowMs - s.startMs;
  if (elapsed < CANDLE_FADE_DURATION_MS && (nowMs - s.lastTickMs) < 30000UL) return;
  s.lastTickMs = nowMs;

  const auto step = candleFadeStep(s.startBrightness, s.startWarmth, elapsed);
  if (step.done) {
    s.active = false;
    Frontlight.setOn(false);
    if (SETTINGS.frontlightOn != 0) {
      SETTINGS.frontlightOn = 0;
      SETTINGS.saveToFile();
    }
    LOG_INF("FL", "Candle Fade finished, light off");
    return;
  }
  if (Frontlight.brightness() != step.brightness) Frontlight.setBrightness(step.brightness);
  if (Frontlight.warmth() != step.warmth) Frontlight.setWarmth(step.warmth);
}

// Lazily interpolates warmth from the schedule via halClock. No-op on boards
// without warm/cold LEDs, when the light or schedule is off, or during Candle Fade.
inline void syncScheduledWarmth() {
  if (!Frontlight.hasColorTemperature() || !Frontlight.isOn() || SETTINGS.frontlightScheduleEnabled == 0 ||
      candleFadeActive()) {
    return;
  }
  uint16_t nowMinutes = 0;
  if (!halClock.localMinutes(nowMinutes, SETTINGS.clockTimezone, SETTINGS.clockDst)) return;
  const uint8_t target = scheduledWarmth(SETTINGS.frontlightScheduleOn * SCHEDULE_STEP_MINUTES,
                                         SETTINGS.frontlightScheduleOff * SCHEDULE_STEP_MINUTES, nowMinutes,
                                         SETTINGS.activeFrontlightWarmth());
  if (target != Frontlight.warmth()) {
    Frontlight.setWarmth(target);
  }
}

}  // namespace frontlight
