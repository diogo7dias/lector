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
//
// A silent reboot happens mid-session with the reader's eyes on the page, so
// the light comes back exactly as it was, ignoring both saved preferences.
// The saved state is not good enough: a wake with Restore Light on Wake off
// leaves the light dark while "was on" stays saved, and re-deriving from it
// would switch the light on under the reader. A real wake is the opposite:
// the reader chose to open the device, and a light that switches itself on in
// a bright room is worse than one they turn on.
//
// Kept separate from HalFrontlight so it can be tested on the host — the HAL
// itself is a thin shim over the SDK FrontlightManager and needs hardware.

namespace frontlight {

struct BootContext {
  bool savedOn = false;
  bool restoreOnWake = false;
  bool silentReboot = false;
  bool litBeforeSilentReboot = false;
};

inline constexpr bool restoreLightOnAtBoot(const BootContext ctx) {
  return ctx.silentReboot ? ctx.litBeforeSilentReboot : ctx.savedOn && ctx.restoreOnWake;
}

}  // namespace frontlight
