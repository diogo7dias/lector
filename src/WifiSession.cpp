#include "WifiSession.h"

#include <Arduino.h>  // delay
#include <WiFi.h>

#include "SilentRestart.h"

void endWifiSession(const WifiReboot target, const bool wasApMode) {
  // Drop the radio first. This is cosmetic for the heap — the reboot below is what
  // actually frees the LWIP/wolfSSL fragmentation — but it stops the link/AP and
  // matches each activity's prior teardown.
  if (wasApMode) {
    WiFi.softAPdisconnect(true);
  } else {
    WiFi.disconnect(false);
  }
  delay(30);
  // The reboot is the real reclaim: a clean boot starts from an unfragmented heap.
  // These do not return.
  if (target == WifiReboot::Reader) {
    silentRestartToReader();
  } else {
    silentRestart();
  }
}
