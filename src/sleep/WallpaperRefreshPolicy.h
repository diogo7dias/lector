#pragma once

#include <cstdint>

// Refresh Wallpaper: one timed wake after a lock that left a wallpaper on the panel,
// which draws the same picture again and sleeps. A gray image drawn straight over a
// menu or a page keeps faint outlines of it; the same image driven a second time,
// after the panel has rested, clears them (upstream #4376). Pure policy, host-tested
// in test/wallpaper_refresh_policy.
namespace wallpaper_refresh {

// How long the panel rests before the second drive.
// Hardware knob: raise it if a minute proves too short for the traces to settle out.
inline constexpr uint64_t DELAY_US = 60ULL * 1000000ULL;
// Below this the extra wake is not worth the charge.
inline constexpr uint16_t MIN_BATTERY_PERCENT = 15;

struct SleepFacts {
  bool settingOn = false;
  bool wallpaperOnPanel = false;  // the lock painted a wallpaper, not a cover or the logo
  uint16_t batteryPercent = 0;
};

inline constexpr bool shouldArm(const SleepFacts& f) {
  return f.settingOn && f.wallpaperOnPanel && f.batteryPercent >= MIN_BATTERY_PERCENT;
}

// A boot is the refresh only when all three hold. The timer cause alone is not proof:
// an X4 Pro has reported a real button unlock as TIMER (see lib/hal/WakeClassify.h), so
// a held power button always wins and the boot stays an ordinary wake.
inline constexpr bool isRefreshWake(const bool armed, const bool timerCause, const bool powerHeld) {
  return armed && timerCause && !powerHeld;
}

}  // namespace wallpaper_refresh
