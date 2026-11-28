#pragma once

#include <cstdint>

namespace web_power_save {

// Drop STA radio from WIFI_PS_NONE to WIFI_PS_MIN_MODEM after 15 s without
// HTTP, WebSocket, or discovery traffic and with no upload or fetch active.
// WIFI_PS_MIN_MODEM wakes on every DTIM beacon (100-300 ms), so mDNS
// multicast and new TCP connections still arrive while idle RX power drops.
inline constexpr uint32_t IDLE_BEFORE_MODEM_SLEEP_MS = 15000;

struct State {
  uint32_t lastActivityMs = 0;
  bool modemSleep = false;

  void reset(const uint32_t nowMs) {
    lastActivityMs = nowMs;
    modemSleep = false;
  }

  // Records traffic and returns true only when the radio must leave
  // WIFI_PS_MIN_MODEM and return to WIFI_PS_NONE.
  bool noteActivity(const uint32_t nowMs) {
    lastActivityMs = nowMs;
    if (!modemSleep) return false;
    modemSleep = false;
    return true;
  }

  // Returns true once when the server has been idle long enough in STA mode
  // to enter WIFI_PS_MIN_MODEM. AP mode and active transfers stay full power.
  bool shouldEnterModemSleep(const uint32_t nowMs, const bool apMode, const bool busy) {
    if (apMode || busy || modemSleep) return false;
    if (static_cast<uint32_t>(nowMs - lastActivityMs) < IDLE_BEFORE_MODEM_SLEEP_MS) return false;
    modemSleep = true;
    return true;
  }
};

}  // namespace web_power_save
