#include "EspNowLink.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

using namespace nearby_position;

namespace {

constexpr const char* LOG_TAG = "NBPS";
// ESP-NOW peers must agree on a channel, and a reader that is not joined to a
// network has no channel of its own to inherit.
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t BROADCAST_MAC[MAC_BYTES] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// The receive callback is a plain C function with no user pointer, so the active
// link has to be reachable from a file-scope variable. Only one link may run at
// a time, which begin() enforces.
EspNowLink* activeLink = nullptr;

void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, const int length) {
  if (!activeLink || !info || !info->src_addr) return;
  // Trampoline into the active link, which does the bounds-checked decode.
  activeLink->handleRawPacket(info->src_addr, data, length);
}

}  // namespace

EspNowLink::~EspNowLink() { end(); }

bool EspNowLink::begin() {
  if (running_) return true;
  if (activeLink) {
    LOG_ERR(LOG_TAG, "Another nearby link is already running");
    return false;
  }
  // The web server and this cannot hold the radio at the same time.
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_ERR(LOG_TAG, "WiFi is already in use; refusing to start ESP-NOW");
    return false;
  }

  queueMutex_ = xSemaphoreCreateMutex();
  if (!queueMutex_) {
    LOG_ERR(LOG_TAG, "Could not create the packet queue mutex");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  radioStarted_ = true;

  if (esp_now_init() != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_init failed");
    end();
    return false;
  }

  activeLink = this;
  if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_register_recv_cb failed");
    end();
    return false;
  }

  uint8_t mac[MAC_BYTES] = {};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) std::memcpy(localMac_.data(), mac, MAC_BYTES);

  running_ = true;
  return true;
}

void EspNowLink::end() {
  if (activeLink == this) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    activeLink = nullptr;
  }

  if (radioStarted_) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    radioStarted_ = false;
  }

  if (queueMutex_) {
    vSemaphoreDelete(queueMutex_);
    queueMutex_ = nullptr;
  }

  queueHead_ = 0;
  queueCount_ = 0;
  running_ = false;
}

void EspNowLink::handleRawPacket(const uint8_t* sourceMac, const uint8_t* data, const int length) {
  if (!sourceMac || !data || length <= 0) return;

  Received received;
  // Decoding here keeps malformed frames out of the queue entirely, and the
  // decoder is bounds-checked against exactly the bytes that arrived.
  if (!decodePacket(data, static_cast<size_t>(length), received.packet)) return;
  std::memcpy(received.sourceMac.data(), sourceMac, MAC_BYTES);

  if (!queueMutex_ || xSemaphoreTake(queueMutex_, 0) != pdTRUE) return;
  if (queueCount_ < QUEUE_CAPACITY) {
    const size_t slot = (queueHead_ + queueCount_) % QUEUE_CAPACITY;
    queue_[slot] = std::move(received);
    queueCount_++;
  }
  // A full queue drops the newest packet. Every message this protocol sends is
  // either retried until acknowledged or repeated on an interval, so a dropped
  // frame costs one retry rather than the session.
  xSemaphoreGive(queueMutex_);
}

bool EspNowLink::nextReceived(Received& received) {
  if (!queueMutex_) return false;
  if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return false;

  bool popped = false;
  if (queueCount_ > 0) {
    received = std::move(queue_[queueHead_]);
    queueHead_ = static_cast<uint8_t>((queueHead_ + 1) % QUEUE_CAPACITY);
    queueCount_--;
    popped = true;
  }

  xSemaphoreGive(queueMutex_);
  return popped;
}

bool EspNowLink::ensurePeer(const std::array<uint8_t, MAC_BYTES>& peerMac) {
  if (esp_now_is_peer_exist(peerMac.data())) return true;

  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, peerMac.data(), MAC_BYTES);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  const esp_err_t result = esp_now_add_peer(&peer);
  if (result != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_add_peer failed: %d", static_cast<int>(result));
    return false;
  }
  return true;
}

bool EspNowLink::send(const PacketType type, const std::array<uint8_t, MAC_BYTES>& peerMac,
                      const CompactPosition& position, const std::string& deviceName) {
  if (!running_) return false;
  if (!ensurePeer(peerMac)) return false;

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  size_t length = 0;
  if (!encodePacket(type, localMac_.data(), position, deviceName, packet.data(), packet.size(), length)) {
    LOG_ERR(LOG_TAG, "Could not encode packet type %d", static_cast<int>(type));
    return false;
  }

  const esp_err_t result = esp_now_send(peerMac.data(), packet.data(), length);
  if (result != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_send failed: %d", static_cast<int>(result));
    return false;
  }
  return true;
}

bool EspNowLink::broadcast(const PacketType type, const CompactPosition& position, const std::string& deviceName) {
  std::array<uint8_t, MAC_BYTES> broadcastMac = {};
  std::memcpy(broadcastMac.data(), BROADCAST_MAC, MAC_BYTES);
  return send(type, broadcastMac, position, deviceName);
}
