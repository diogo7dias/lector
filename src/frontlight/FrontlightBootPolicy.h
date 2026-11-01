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

}  // namespace frontlight
