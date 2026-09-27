#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

// A WiFi activity's way out: when the radio is on, drop the station (radio kept
// up), let the disconnect settle, then silentRestart(). No-op with WiFi off.
void teardownWifiAndRestart();
