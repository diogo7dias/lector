#pragma once

#include <NearbyTransfer.h>

#include <array>
#include <cstdint>
#include <string>

#include "NearbyPositionProtocol.h"

/**
 * The radio behind Nearby Position Sync: bring ESP-NOW up, hand decoded packets
 * to the activity, send packets to a peer, tear it all down again.
 *
 * Everything about what to say and when lives in NearbyPositionSession. This
 * only moves bytes, so the protocol stays testable on a host where no radio
 * exists.
 *
 * The radio itself is the SDK's EspNowTransport, the same one Nearby File
 * Transfer uses. Bringing ESP-NOW up takes more than esp_now_init: a station
 * that has joined no network keeps modem sleep on, and a sleeping radio misses
 * the broadcasts that the two readers find each other with. This class had its
 * own setup that omitted that, so two readers searching side by side never heard
 * one another. One shared transport means the setup can only be right or wrong
 * for both features at once.
 *
 * Frames arrive on the WiFi task and are queued there; the activity drains them
 * from its own loop, so no rendering or EPUB code runs on the radio callback.
 */
class EspNowLink {
 public:
  /** A decoded packet plus the address it actually came from. */
  struct Received {
    nearby_position::PacketView packet;
    std::array<uint8_t, nearby_position::MAC_BYTES> sourceMac = {};
  };

  /**
   * Powers up the radio and starts listening.
   *
   * Fails when WiFi is already in use, since the web server and this cannot hold
   * the radio at the same time.
   */
  bool begin();

  /** Stops listening and powers the radio back down. Safe to call twice. */
  void end();

  bool isRunning() const { return transport_.started(); }

  /** This device's own MAC, so the session can discard its own broadcasts. */
  const std::array<uint8_t, nearby_position::MAC_BYTES>& localMac() const { return localMac_; }

  /**
   * Sends one packet, adding the peer if it is not registered yet. A broadcast
   * goes to every listening device; anything else goes only to `peerMac`.
   */
  bool send(nearby_position::PacketType type, const std::array<uint8_t, nearby_position::MAC_BYTES>& peerMac,
            const nearby_position::CompactPosition& position, const std::string& deviceName);
  bool broadcast(nearby_position::PacketType type, const nearby_position::CompactPosition& position,
                 const std::string& deviceName);

  /**
   * Pops the next received packet, skipping frames that are not ours or do not
   * decode. Call it from the activity loop until it returns false.
   */
  bool nextReceived(Received& received);

 private:
  freeink::nearby::EspNowTransport transport_;
  std::array<uint8_t, nearby_position::MAC_BYTES> localMac_ = {};
};
