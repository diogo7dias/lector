#pragma once

// The correct way to end an activity's WiFi session. The ~45 KB of LWIP/wolfSSL
// heap fragmentation a WiFi task leaves behind is reclaimed ONLY by the reboot
// (silentRestart) — the disconnect is cosmetic. Seven WiFi activities used to
// hand-roll this same "disconnect + settle + silent-restart" teardown in onExit(),
// with the "reboot is the real reclaim" rule living in seven comments. This is the
// one place that rule lives now.
//
// Callers keep their own guard (only tear down if the radio was actually brought
// up — WiFi.getMode() != WIFI_MODE_NULL, or an activity-specific flag). This
// function performs the teardown unconditionally and, on the reboot, DOES NOT
// RETURN (ESP.restart()).
//
// Deep sleep does NOT use this: its wake is a full chip reset that already clears
// the heap, so it powers the radio down without rebooting (see main.cpp).
enum class WifiReboot { Home, Reader };

// Drop the radio and silent-restart to `target` (home screen or the open book).
// Pass wasApMode=true to stop a SoftAP cleanly (WiFi.softAPdisconnect) instead of
// a station disconnect. Never returns.
void endWifiSession(WifiReboot target, bool wasApMode = false);
