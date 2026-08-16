#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Wire format for Nearby Position Sync: two readers a few metres apart trade
 * their place in the same book directly over ESP-NOW, with no WiFi network,
 * server, or account involved.
 *
 * This header is deliberately free of ESP-IDF, Arduino, and rendering headers so
 * the whole format can be exercised by host tests. The radio lives in
 * EspNowLink; the screens live in NearbyPositionSyncActivity.
 *
 * The layout is byte-compatible with CrossInk's Nearby Position Sync, so a
 * lector device and a CrossInk device can sync with each other. Treat the
 * magic bytes, field order, and sizes below as a contract with that firmware:
 * changing any of them breaks cross-firmware sync and needs PROTOCOL_VERSION to
 * move with it.
 *
 * Packet header, 14 bytes:
 *   [0..3]   magic 'C' 'I' 'B' 'P'
 *   [4]      protocol version
 *   [5]      packet type
 *   [6..7]   payload length, little-endian uint16
 *   [8..13]  sender device MAC
 *
 * Payload by type:
 *   HELLO     the 32-byte document hash, so a peer reading a different book is
 *             recognised before any position is exchanged
 *   POSITION  a serialized CompactPosition: where the sender is in the book
 *   APPLY     the same CompactPosition, meaning "move to this"
 *   ACK       empty
 *   NAME      1 to MAX_DEVICE_NAME_BYTES bytes of display name
 */
namespace nearby_position {

constexpr size_t MAC_BYTES = 6;
constexpr size_t PACKET_HEADER_BYTES = 14;
constexpr size_t MAX_PACKET_BYTES = 250;
constexpr uint8_t PROTOCOL_VERSION = 1;

constexpr size_t DOCUMENT_HASH_BYTES = 32;
constexpr size_t MAX_DEVICE_NAME_BYTES = 20;
constexpr size_t MAX_ANCHOR_BYTES = 48;
constexpr size_t MAX_XPATH_BYTES = 120;

// Progress is carried as a fixed-point fraction of the book rather than a float,
// so both ends agree on the value bit for bit.
constexpr uint32_t PERCENTAGE_SCALE = 1000000;

enum class PacketType : uint8_t {
  HELLO = 1,
  POSITION = 2,
  APPLY = 3,
  ACK = 4,
  NAME = 5,
};

/**
 * One reading position, sized for a single ESP-NOW packet.
 *
 * The fields mirror what KOSync already round-trips, so a received position is
 * applied through the same ProgressMapper path a KOSync pull uses: the xpath is
 * the authoritative anchor, and the spine/page/paragraph hints are fallbacks for
 * when it cannot be resolved against the local layout.
 */
struct CompactPosition {
  std::array<char, DOCUMENT_HASH_BYTES + 1> documentHash = {};
  uint32_t percentageQ = 0;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t totalPages = 1;
  uint16_t paragraphIndex = 0;
  uint16_t liIndex = 0;
  bool hasParagraphIndex = false;
  bool hasLiIndex = false;
  std::array<char, MAX_ANCHOR_BYTES + 1> anchor = {};
  std::array<char, MAX_XPATH_BYTES + 1> xpath = {};
};

/** A decoded packet. Only the fields the type carries are populated. */
struct PacketView {
  PacketType type = PacketType::HELLO;
  std::array<uint8_t, MAC_BYTES> deviceMac = {};
  CompactPosition position;
  std::string deviceName;
};

/**
 * Serializes one packet into `output`.
 *
 * `position` is read for HELLO, POSITION, and APPLY; `deviceName` only for NAME,
 * where it is truncated to MAX_DEVICE_NAME_BYTES. Over-long anchors and xpaths
 * are truncated rather than rejected, since a shortened anchor still resolves or
 * degrades to the numeric fallbacks. A malformed document hash is refused
 * outright: without it the receiver cannot tell which book the position belongs
 * to.
 *
 * Returns false and leaves `outputLength` untouched if the packet cannot be
 * built, including when it would not fit in `capacity`.
 */
bool encodePacket(PacketType type, const uint8_t* deviceMac, const CompactPosition& position,
                  const std::string& deviceName, uint8_t* output, size_t capacity, size_t& outputLength);

/**
 * Parses one received packet.
 *
 * Every field is bounds-checked against the bytes actually received: this runs
 * on whatever arrives over the air, including packets from other firmware and
 * corrupted frames. A packet with trailing bytes past its declared payload is
 * rejected rather than parsed leniently.
 */
bool decodePacket(const uint8_t* data, size_t length, PacketView& output);

/** Copies `hash` into `position`, truncating at DOCUMENT_HASH_BYTES. */
void setDocumentHash(CompactPosition& position, const std::string& hash);
/** Copies `anchor` into `position`, truncating at MAX_ANCHOR_BYTES. */
void setAnchor(CompactPosition& position, const std::string& anchor);
/** Copies `xpath` into `position`, truncating at MAX_XPATH_BYTES. */
void setXpath(CompactPosition& position, const std::string& xpath);

/** Converts a 0.0 to 1.0 fraction to wire form, clamping anything outside it. */
uint32_t percentageToQ(float percentage);
/** Converts wire form back to a 0.0 to 1.0 fraction. */
float percentageFromQ(uint32_t percentageQ);

}  // namespace nearby_position
