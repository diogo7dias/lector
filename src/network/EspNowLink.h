#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "NearbyPositionProtocol.h"

/**
 * The radio behind Nearby Position Sync: bring ESP-NOW up, hand decoded packets
 * to a callback, send bytes to a peer, tear it all down again.
 *
 * Everything about what to say and when lives in NearbyPositionSession. This
 * only moves bytes, so the protocol stays testable on a host where no radio
 * exists.
 *
 * Packets arrive on the WiFi task, not the activity's. Received frames are
 * decoded and queued behind a mutex here; the activity drains the queue from its
 * own loop, so no rendering or EPUB code ever runs on the radio callback.
 */
class EspNowLink {
 public:
  /** A decoded packet plus the address it actually came from. */
  struct Received {
    nearby_position::PacketView packet;
    std::array<uint8_t, nearby_position::MAC_BYTES> sourceMac = {};
  };

  ~EspNowLink();

  /**
   * Powers up the radio and starts listening.
   *
   * Fails when WiFi is already in use, since the web server and this cannot hold
   * the radio at the same time, and when only one EspNowLink may be active.
   */
  bool begin();

  /** Stops listening and powers the radio back down. Safe to call twice. */
  void end();

  bool isRunning() const { return running_; }

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
   * Pops the next received packet, or returns false when none is waiting. Call
   * it from the activity loop until it returns false.
   */
  bool nextReceived(Received& received);

  /**
   * Decodes one raw frame and queues it. Called from the ESP-NOW receive
   * callback on the WiFi task, so it does nothing but decode and enqueue.
   */
  void handleRawPacket(const uint8_t* sourceMac, const uint8_t* data, int length);

 private:
  static constexpr size_t QUEUE_CAPACITY = 8;

  bool ensurePeer(const std::array<uint8_t, nearby_position::MAC_BYTES>& peerMac);

  bool running_ = false;
  bool radioStarted_ = false;
  std::array<uint8_t, nearby_position::MAC_BYTES> localMac_ = {};

  SemaphoreHandle_t queueMutex_ = nullptr;
  std::array<Received, QUEUE_CAPACITY> queue_ = {};
  uint8_t queueHead_ = 0;
  uint8_t queueCount_ = 0;
};
