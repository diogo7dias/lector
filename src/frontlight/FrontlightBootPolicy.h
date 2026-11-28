#pragma once

// Pure policy: should the frontlight come back lit when the firmware starts?
//
// Brightness and warmth are always restored, so they are not part of this
// decision — only the on/off state is. The inputs:
//
//   savedOn        the light was on when the settings were last written
//   restoreOnWake  the reader asked for the light to come back by itself
//   silentReboot   this start is an invisible maintenance restart (heap
//                  defrag), not a wake the reader performed
//   litBeforeSilentReboot  the live light state captured just before that
//                  restart; meaningless when silentReboot is false
//   scheduledOn    the daily schedule is on and the clock says now is inside it
//
// A silent reboot happens mid-session with the reader's eyes on the page, so
// the light comes back exactly as it was, ignoring both saved preferences.
// The saved state is not good enough: a wake with Restore Light on Wake off
// leaves the light dark while "was on" stays saved, and re-deriving from it
// would switch the light on under the reader. A real wake is the opposite:
// the reader chose to open the device, and a light that switches itself on in
// a bright room is worse than one they turn on, unless they scheduled it. The
// schedule is only read at a wake: a light changed by hand stays that way until
// the next one.
//
// Kept separate from HalFrontlight so it can be tested on the host — the HAL
// itself is a thin shim over the SDK FrontlightManager and needs hardware.

namespace frontlight {

struct BootContext {
  bool savedOn = false;
  bool restoreOnWake = false;
  bool silentReboot = false;
  bool litBeforeSilentReboot = false;
  bool scheduledOn = false;
};

inline constexpr bool restoreLightOnAtBoot(const BootContext ctx) {
  return ctx.silentReboot ? ctx.litBeforeSilentReboot : (ctx.savedOn && ctx.restoreOnWake) || ctx.scheduledOn;
}

// Schedule times are stored as quarter hours since local midnight, which fits the
// settings' uint8_t rows and is finer than anyone plans an evening.
inline constexpr int SCHEDULE_STEP_MINUTES = 15;
inline constexpr int SCHEDULE_LAST_STEP = 95;  // 23:45

// Minutes since midnight. A window may cross midnight (20:00 to 07:00); equal ends are empty.
inline constexpr bool insideSchedule(const int onMinutes, const int offMinutes, const int nowMinutes) {
  return onMinutes < offMinutes ? nowMinutes >= onMinutes && nowMinutes < offMinutes
                                : onMinutes != offMinutes && (nowMinutes >= onMinutes || nowMinutes < offMinutes);
}

// Sunset-to-dawn warmth curve (0..100). Outside the schedule, returns baseWarmth.
// Inside the schedule, ramps linearly from baseWarmth to peakWarmth over the first
// min(span, 90) minutes, holds peakWarmth overnight, and (for windows >= 120 min)
// eases back to baseWarmth over the final 30 minutes before offMinutes.
inline constexpr uint8_t scheduledWarmth(const int onMinutes, const int offMinutes, const int nowMinutes,
                                         const uint8_t baseWarmth, const uint8_t peakWarmth = 100) {
  if (!insideSchedule(onMinutes, offMinutes, nowMinutes) || peakWarmth <= baseWarmth) return baseWarmth;
  constexpr int kDay = 24 * 60;
  const int span = (offMinutes - onMinutes + kDay) % kDay;
  if (span <= 0) return baseWarmth;
  const int elapsed = (nowMinutes - onMinutes + kDay) % kDay;
  const int delta = static_cast<int>(peakWarmth) - static_cast<int>(baseWarmth);
  const int ramp = span < 90 ? span : 90;
  if (elapsed < ramp) {
    return static_cast<uint8_t>(baseWarmth + (delta * elapsed + ramp / 2) / ramp);
  }
  constexpr int kDawnCool = 30;
  if (span >= 120 && elapsed > span - kDawnCool) {
    const int rem = span - elapsed;
    return static_cast<uint8_t>(baseWarmth + (delta * rem + kDawnCool / 2) / kDawnCool);
  }
  return peakWarmth;
}

inline constexpr uint32_t CANDLE_FADE_DURATION_MS = 25UL * 60UL * 1000UL;

struct CandleFadeStep {
  uint8_t brightness = 0;
  uint8_t warmth = 100;
  bool done = true;
};

// One step of the Candle Fade timer: dims brightness linearly to 0 while warming
// color temperature linearly to 100 over durationMs.
inline constexpr CandleFadeStep candleFadeStep(const uint8_t startBrightness, const uint8_t startWarmth,
                                               const uint32_t elapsedMs,
                                               const uint32_t durationMs = CANDLE_FADE_DURATION_MS) {
  if (durationMs == 0 || elapsedMs >= durationMs) return {0, 100, true};
  const uint32_t permille = static_cast<uint32_t>((static_cast<uint64_t>(elapsedMs) * 1000ULL) / durationMs);
  const uint8_t brightness =
      static_cast<uint8_t>((static_cast<uint32_t>(startBrightness) * (1000U - permille) + 500U) / 1000U);
  const uint8_t warmth =
      startWarmth >= 100
          ? 100
          : static_cast<uint8_t>(startWarmth + ((static_cast<uint32_t>(100U - startWarmth) * permille + 500U) / 1000U));
  return {brightness, warmth, false};
}

struct LightProfile {
  uint8_t brightness = 0;
  uint8_t warmth = 0;
};

// Selects the day or night-mode brightness/warmth pair. Until the reader edits the light
// in Night Mode (`nightCustomized` is false), Night Mode inherits the day pair unchanged.
inline constexpr LightProfile activeLightProfile(const bool nightMode, const bool nightCustomized,
                                                 const uint8_t dayBrightness, const uint8_t dayWarmth,
                                                 const uint8_t nightBrightness, const uint8_t nightWarmth) {
  return (nightMode && nightCustomized) ? LightProfile{nightBrightness, nightWarmth}
                                        : LightProfile{dayBrightness, dayWarmth};
}

}  // namespace frontlight
