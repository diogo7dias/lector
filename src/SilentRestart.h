#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)
// Where Back from the screen on top lands (ActivityManager::backTrail()): its opener, on the
// row that opened it, or Home on the screen's row. The way out of a radio screen.
void silentRestartBack();
void silentRestartToTrail(uint64_t trail);  // a trail of the caller's own (BackTrail.h)

// Whether a WiFi screen's way out reboots to give the heap back. Switching the radio off
// does not undo the fragmentation, so a screen that knows it used WiFi says so.
inline bool wifiRestartNeeded(const bool radioOn, const bool wifiUsed) { return radioOn || wifiUsed; }

// A WiFi activity's way out: drop the station (radio kept up), let the disconnect settle,
// then restart (by default where Back lands). No-op with WiFi off, unless wifiUsed.
void teardownWifiAndRestart(void (*restart)() = silentRestartBack, bool wifiUsed = false);

// enterDeepSleep() calls this once it commits to sleeping: deep sleep resets the chip
// anyway, so a WiFi screen's silentRestart() in onExit() must not reboot first.
void markDeepSleepCommitted();

enum class RestartTarget : uint8_t { Home, Reader };

// What the previous session left in RTC memory, read and cleared once, early in setup().
// On a silent boot it also arms the rescue timer that reboots a restart stuck before
// its first frame.
struct RestartBoot {
  bool silent = false;  // heap-defrag restart: no splash, the panel keeps its frame
  RestartTarget target = RestartTarget::Home;
  uint64_t backTrail = 0;                    // Home: where to land (ActivityManager::followBackTrail); 0 is plain Home
  void (*readerMenu)(void*, int) = nullptr;  // Reader: the menu to reopen (MenuReturn::reopen)
  int readerMenuRow = 0;
  bool lightOn = false;  // the frontlight was on when the restart was armed
};
RestartBoot takeRestartBoot();

// Breadcrumbs for a silent boot in progress; no-ops on any other boot.
void noteRestartDisplayUp();
void noteRestartLoopPass();
