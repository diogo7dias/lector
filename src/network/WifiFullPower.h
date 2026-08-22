#pragma once

#ifndef SIMULATOR
#include <esp_wifi.h>
#endif

// Holds the radio at full power for as long as a transfer is running.
//
// The station defaults to WIFI_PS_MIN_MODEM, which parks the radio between
// beacons. That is right for a reader sitting on the home screen and wrong for
// a 5 MB firmware image or a font family: the parked radio drops packets the
// far end then retransmits, and on a marginal link the transfer stalls until
// the socket gives up. Every download therefore takes one of these for its
// duration, and the last one out puts power saving back.
//
// Refcounted because the scopes nest: a firmware install takes one, and the
// fetch it drives takes another.
class WifiFullPower {
 public:
  WifiFullPower() {
    if (depth++ == 0) {
#ifndef SIMULATOR
      esp_wifi_set_ps(WIFI_PS_NONE);
#endif
    }
  }

  ~WifiFullPower() {
    if (--depth == 0) {
#ifndef SIMULATOR
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#endif
    }
  }

  WifiFullPower(const WifiFullPower&) = delete;
  WifiFullPower& operator=(const WifiFullPower&) = delete;

 private:
  // Every network transfer runs on the same task, so a plain counter is enough;
  // nothing here is reached from an ISR.
  static inline int depth = 0;
};
