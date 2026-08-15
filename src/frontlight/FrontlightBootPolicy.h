#pragma once

// Pure policy: should the frontlight come back lit when the firmware starts?
//
// Brightness and warmth are always restored, so they are not part of this
// decision — only the on/off state is. Three inputs decide it:
//
//   savedOn        the light was on when the settings were last written
//   restoreOnWake  the reader asked for the light to come back by itself
//   silentReboot   this start is an invisible maintenance restart (heap
//                  defrag), not a wake the reader performed
//
// The silent-reboot case is the subtle one. That restart happens mid-session
// with the reader's eyes on the page; going dark there would read as a fault,
// so the live state wins over the Restore Light on Wake preference. A real
// wake is the opposite: the reader chose to open the device, and a light that
// switches itself on in a bright room is worse than one they turn on.
//
// Kept separate from HalFrontlight so it can be tested on the host — the HAL
// itself is a thin shim over the SDK FrontlightManager and needs hardware.

namespace frontlight {

struct BootContext {
  bool savedOn = false;
  bool restoreOnWake = false;
  bool silentReboot = false;
};

inline constexpr bool restoreLightOnAtBoot(const BootContext ctx) {
  return ctx.savedOn && (ctx.restoreOnWake || ctx.silentReboot);
}

}  // namespace frontlight
