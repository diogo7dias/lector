#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();            // home screen
void silentRestartToReader();    // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToSettings();  // settings screen

// Writes the panel's anti-ghost budget to APP_STATE. silentRestart() does this itself;
// USB Drive calls it before lending the card, after which no write can land.
void persistAntiGhostBudget();

// A WiFi activity's way out: when the radio is on, drop the station (radio kept
// up), let the disconnect settle, then restart. No-op with WiFi off.
void teardownWifiAndRestart(void (*restart)() = silentRestart);

// enterDeepSleep() calls this once it commits to sleeping: deep sleep resets the chip
// anyway, so a WiFi screen's silentRestart() in onExit() must not reboot first.
void markDeepSleepCommitted();

enum class RestartTarget : uint8_t { Home, Reader, Settings };

// What the previous session left in RTC memory, read and cleared once, early in setup().
// On a silent boot it also arms the rescue timer that reboots a restart stuck before
// its first frame.
struct RestartBoot {
  bool silent = false;  // heap-defrag restart: no splash, the panel keeps its frame
  RestartTarget target = RestartTarget::Home;
  bool lightOn = false;  // the frontlight was on when the restart was armed
  const char* trailStage = "none";
  const char* rescuedStage = "none";
  bool rescued = false;              // the rescue timer had to finish the last restart
  bool trailWorthRecording = false;  // the last restart stopped somewhere unexpected
};
RestartBoot takeRestartBoot();

// Breadcrumbs for a silent boot in progress; no-ops on any other boot.
void noteRestartDisplayUp();
void noteRestartLoopPass();
