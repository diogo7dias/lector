#pragma once

#include <cstdint>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HalStorage.h"

// The shared button bindings (Double-click power, and the two Confirm holds) were written
// against the EPUB reader, which owns the in-book menu every one of them acts through. The
// TXT and XTC readers have no such menu, so most bindings can never run there — but the
// settings are global, so a user who binds one and then opens a .txt gets a button that
// does nothing at all and says nothing about it.
//
// This resolves a binding into the subset a reader without the in-book menu CAN honour:
// the ones that need nothing but global state. Everything else resolves to None, and the
// caller reports it the way the EPUB reader reports a bound-but-impossible function — with
// the "not available" pop-up, so the press is never silently dropped.
//
// Text Settings is deliberately NOT in the subset. The TXT reader keeps its own font and
// size (txtSdFontFamilyName / txtFontPointSize, edited from its Confirm pop-up), so the
// global Text Settings screen would change margins and alignment on the page while its
// font rows did nothing — worse than saying the binding does not apply here.
namespace simple_reader_shortcut {

enum class Action : uint8_t {
  None,             // Not runnable in this reader; caller says so.
  ToggleStatusBar,  // Flip the global status bar master switch.
  WallpaperHold,    // Stop (or resume) picking a new wallpaper at each sleep.
};

// supportsStatusBarToggle: the TXT reader draws the shared status bar and honours
// SETTINGS.statusBarEnabled(), so the toggle means something there. The XTC reader has its
// own three-way xtcStatusBarMode instead, which a two-state toggle cannot express, so it
// passes false and the binding reports itself unavailable rather than flipping a switch
// that reader never reads.
inline Action resolve(const uint8_t function, const bool supportsStatusBarToggle) {
  switch (function) {
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
      return supportsStatusBarToggle ? Action::ToggleStatusBar : Action::None;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
      // Same availability test the in-book menu and the EPUB reader use: there must be a
      // wallpaper the lock screen actually showed, and it must still be on the card.
      return (!APP_STATE.lastSleepWallpaperPath.empty() && Storage.exists(APP_STATE.lastSleepWallpaperPath.c_str()))
                 ? Action::WallpaperHold
                 : Action::None;
    default:
      return Action::None;
  }
}

// True when the double-click detector is worth arming: every power click pays the
// detector's ~280 ms hold-back while it waits for a second one, so a reader that could
// only answer "not available" leaves it disarmed and keeps its clicks instant.
inline bool armsDoubleClick(const bool supportsStatusBarToggle) {
  return SETTINGS.powerDoubleClickBound() &&
         resolve(SETTINGS.doubleClickPowerFunction, supportsStatusBarToggle) != Action::None;
}

}  // namespace simple_reader_shortcut
