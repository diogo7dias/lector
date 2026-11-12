#pragma once

#include <cstdint>

namespace ws_idle {

// A client that vanishes mid-upload (phone asleep, Wi-Fi lost) would keep its
// slot and the upload lock until TCP gives up. Clients send START right after
// connecting and then data without pause, so close one that has sent nothing
// for two minutes; a browser runs timers in a hidden tab only once a minute.
// A ping heartbeat would not do: the Calibre plugin does not answer pings while
// it uploads.
constexpr uint32_t IDLE_TIMEOUT_MS = 120000;

// lastActivity holds millis() of each slot's last event, 0 for a free slot. The
// server's DISCONNECTED event clears a closed slot's time.
template <class Server>
void closeIdleClients(Server& ws, uint32_t* lastActivity, const uint8_t slots, const uint32_t now) {
  for (uint8_t i = 0; i < slots; i++) {
    if (lastActivity[i] == 0) {
      // Time a new client from its first sight, so one that never finishes the
      // handshake (and so never raises an event) is closed too.
      if (ws.clientIsConnected(i)) lastActivity[i] = now;
    } else if (now - lastActivity[i] > IDLE_TIMEOUT_MS && ws.clientIsConnected(i)) {
      ws.disconnect(i);
    }
  }
}

}  // namespace ws_idle
