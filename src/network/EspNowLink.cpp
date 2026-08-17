#include "EspNowLink.h"

#include <Logging.h>
#include <WiFi.h>

#include <cstring>

using namespace nearby_position;

namespace {

constexpr const char* LOG_TAG = "NBPS";
// ESP-NOW peers must agree on a channel, and a reader that is not joined to a
// network has no channel of its own to inherit. Nearby File Transfer uses the
// same one, so a reader can be found by either feature.
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr std::array<uint8_t, MAC_BYTES> BROADCAST_MAC = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

}  // namespace

bool EspNowLink::begin() {
  if (transport_.started()) return true;

  // The web server and this cannot hold the radio at the same time.
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_ERR(LOG_TAG, "WiFi is already in use; refusing to start ESP-NOW");
    return false;
  }

  if (!transport_.begin(ESPNOW_CHANNEL)) {
    LOG_ERR(LOG_TAG, "Could not start ESP-NOW");
    return false;
  }

  uint8_t mac[MAC_BYTES] = {};
  if (!transport_.localMac(mac)) {
    // Every incoming packet is matched against this address to drop the device's
    // own broadcasts. All zeroes would match every packet whose sender also failed
    // to read its MAC, and the two readers would discard each other in silence.
    LOG_ERR(LOG_TAG, "Could not read the local MAC; refusing to start");
    transport_.end();
    return false;
  }
  std::memcpy(localMac_.data(), mac, MAC_BYTES);
  LOG_INF(LOG_TAG, "ESP-NOW up on channel %u as %02x:%02x:%02x:%02x:%02x:%02x", ESPNOW_CHANNEL, mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);

  framesHeard_ = 0;
  framesNotDecoded_ = 0;
  framesSent_ = 0;
  return true;
}

void EspNowLink::end() { transport_.end(); }

bool EspNowLink::nextReceived(Received& received) {
  freeink::nearby::EspNowTransport::Event raw;
  while (transport_.poll(raw)) {
    framesHeard_++;
    // Anything that does not decode is dropped here rather than reaching the
    // session: the radio hears every ESP-NOW frame in the room, including the
    // file transfer's, which shares this channel.
    if (!decodePacket(raw.data.data(), raw.length, received.packet)) {
      framesNotDecoded_++;
      LOG_INF(LOG_TAG, "Heard %u bytes that are not a position packet", static_cast<unsigned>(raw.length));
      continue;
    }
    LOG_INF(LOG_TAG, "Received packet type %d from %02x:%02x:%02x:%02x:%02x:%02x",
            static_cast<int>(received.packet.type), raw.sourceMac[0], raw.sourceMac[1], raw.sourceMac[2],
            raw.sourceMac[3], raw.sourceMac[4], raw.sourceMac[5]);
    std::memcpy(received.sourceMac.data(), raw.sourceMac.data(), MAC_BYTES);
    return true;
  }
  return false;
}

bool EspNowLink::send(const PacketType type, const std::array<uint8_t, MAC_BYTES>& peerMac,
                      const CompactPosition& position, const std::string& deviceName) {
  if (!transport_.started()) return false;

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  size_t length = 0;
  if (!encodePacket(type, localMac_.data(), position, deviceName, packet.data(), packet.size(), length)) {
    LOG_ERR(LOG_TAG, "Could not encode packet type %d", static_cast<int>(type));
    return false;
  }

  if (!transport_.send(peerMac.data(), packet.data(), length)) {
    LOG_ERR(LOG_TAG, "Could not send packet type %d", static_cast<int>(type));
    return false;
  }
  framesSent_++;
  return true;
}

bool EspNowLink::broadcast(const PacketType type, const CompactPosition& position, const std::string& deviceName) {
  return send(type, BROADCAST_MAC, position, deviceName);
}
