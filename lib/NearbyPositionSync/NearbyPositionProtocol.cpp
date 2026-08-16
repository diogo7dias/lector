#include "NearbyPositionProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nearby_position {
namespace {

constexpr uint8_t MAGIC[4] = {'C', 'I', 'B', 'P'};
constexpr uint8_t FLAG_PARAGRAPH = 1 << 0;
constexpr uint8_t FLAG_LI = 1 << 1;

// Everything in a serialized position except the two length-prefixed strings.
constexpr size_t POSITION_FIXED_BYTES = DOCUMENT_HASH_BYTES + 4 + 2 + 2 + 2 + 1 + 2 + 2 + 1 + 1;

void writeU16(uint8_t*& cursor, const uint16_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFF);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeU32(uint8_t*& cursor, const uint32_t value) {
  *cursor++ = static_cast<uint8_t>(value & 0xFF);
  *cursor++ = static_cast<uint8_t>((value >> 8) & 0xFF);
  *cursor++ = static_cast<uint8_t>((value >> 16) & 0xFF);
  *cursor++ = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool readU16(const uint8_t*& cursor, size_t& remaining, uint16_t& value) {
  if (remaining < 2) return false;
  value = static_cast<uint16_t>(cursor[0]) | static_cast<uint16_t>(static_cast<uint16_t>(cursor[1]) << 8);
  cursor += 2;
  remaining -= 2;
  return true;
}

bool readU32(const uint8_t*& cursor, size_t& remaining, uint32_t& value) {
  if (remaining < 4) return false;
  value = static_cast<uint32_t>(cursor[0]) | (static_cast<uint32_t>(cursor[1]) << 8) |
          (static_cast<uint32_t>(cursor[2]) << 16) | (static_cast<uint32_t>(cursor[3]) << 24);
  cursor += 4;
  remaining -= 4;
  return true;
}

/** Length of a NUL-terminated field, never reading past its fixed capacity. */
size_t boundedLength(const char* text, const size_t maxBytes) {
  if (!text) return 0;
  size_t length = 0;
  while (length < maxBytes && text[length] != '\0') length++;
  return length;
}

void copyBounded(char* destination, const size_t destinationSize, const char* source, const size_t sourceLength) {
  if (!destination || destinationSize == 0) return;
  const size_t copied = std::min(destinationSize - 1, sourceLength);
  if (copied > 0 && source) std::memcpy(destination, source, copied);
  destination[copied] = '\0';
}

bool serializePosition(const CompactPosition& position, uint8_t* output, const size_t capacity, size_t& outputLength) {
  if (!output || capacity < POSITION_FIXED_BYTES) return false;
  // Without a full-length hash the receiver cannot identify the book, and a
  // position applied to the wrong book would throw away someone's place.
  if (boundedLength(position.documentHash.data(), DOCUMENT_HASH_BYTES) != DOCUMENT_HASH_BYTES) return false;

  const size_t anchorLength = boundedLength(position.anchor.data(), MAX_ANCHOR_BYTES);
  const size_t xpathLength = boundedLength(position.xpath.data(), MAX_XPATH_BYTES);
  if (POSITION_FIXED_BYTES + anchorLength + xpathLength > capacity) return false;

  uint8_t* cursor = output;
  std::memcpy(cursor, position.documentHash.data(), DOCUMENT_HASH_BYTES);
  cursor += DOCUMENT_HASH_BYTES;

  writeU32(cursor, std::min(position.percentageQ, PERCENTAGE_SCALE));
  writeU16(cursor, position.spineIndex);
  writeU16(cursor, position.pageNumber);
  writeU16(cursor, std::max<uint16_t>(1, position.totalPages));

  uint8_t flags = 0;
  if (position.hasParagraphIndex) flags |= FLAG_PARAGRAPH;
  if (position.hasLiIndex) flags |= FLAG_LI;
  *cursor++ = flags;
  writeU16(cursor, position.paragraphIndex);
  writeU16(cursor, position.liIndex);

  *cursor++ = static_cast<uint8_t>(anchorLength);
  if (anchorLength > 0) {
    std::memcpy(cursor, position.anchor.data(), anchorLength);
    cursor += anchorLength;
  }
  *cursor++ = static_cast<uint8_t>(xpathLength);
  if (xpathLength > 0) {
    std::memcpy(cursor, position.xpath.data(), xpathLength);
    cursor += xpathLength;
  }

  outputLength = static_cast<size_t>(cursor - output);
  return true;
}

bool parsePosition(const uint8_t* data, const size_t length, CompactPosition& output) {
  if (!data || length < POSITION_FIXED_BYTES) return false;

  const uint8_t* cursor = data;
  size_t remaining = length;

  copyBounded(output.documentHash.data(), output.documentHash.size(), reinterpret_cast<const char*>(cursor),
              DOCUMENT_HASH_BYTES);
  cursor += DOCUMENT_HASH_BYTES;
  remaining -= DOCUMENT_HASH_BYTES;

  if (!readU32(cursor, remaining, output.percentageQ) || !readU16(cursor, remaining, output.spineIndex) ||
      !readU16(cursor, remaining, output.pageNumber) || !readU16(cursor, remaining, output.totalPages)) {
    return false;
  }
  output.percentageQ = std::min(output.percentageQ, PERCENTAGE_SCALE);
  output.totalPages = std::max<uint16_t>(1, output.totalPages);

  if (remaining < 1) return false;
  const uint8_t flags = *cursor++;
  remaining--;
  output.hasParagraphIndex = (flags & FLAG_PARAGRAPH) != 0;
  output.hasLiIndex = (flags & FLAG_LI) != 0;

  if (!readU16(cursor, remaining, output.paragraphIndex) || !readU16(cursor, remaining, output.liIndex)) return false;

  if (remaining < 1) return false;
  const uint8_t anchorLength = *cursor++;
  remaining--;
  if (anchorLength > MAX_ANCHOR_BYTES || remaining < static_cast<size_t>(anchorLength) + 1) return false;
  copyBounded(output.anchor.data(), output.anchor.size(), reinterpret_cast<const char*>(cursor), anchorLength);
  cursor += anchorLength;
  remaining -= anchorLength;

  const uint8_t xpathLength = *cursor++;
  remaining--;
  // Exactly the declared xpath must remain: anything else means the packet is
  // not the shape it claims to be.
  if (xpathLength > MAX_XPATH_BYTES || remaining != xpathLength) return false;
  copyBounded(output.xpath.data(), output.xpath.size(), reinterpret_cast<const char*>(cursor), xpathLength);
  return true;
}

bool isKnownType(const uint8_t value, PacketType& type) {
  switch (value) {
    case static_cast<uint8_t>(PacketType::HELLO):
    case static_cast<uint8_t>(PacketType::POSITION):
    case static_cast<uint8_t>(PacketType::APPLY):
    case static_cast<uint8_t>(PacketType::ACK):
    case static_cast<uint8_t>(PacketType::NAME):
      type = static_cast<PacketType>(value);
      return true;
    default:
      return false;
  }
}

}  // namespace

void setDocumentHash(CompactPosition& position, const std::string& hash) {
  copyBounded(position.documentHash.data(), position.documentHash.size(), hash.data(),
              std::min(hash.size(), DOCUMENT_HASH_BYTES));
}

void setAnchor(CompactPosition& position, const std::string& anchor) {
  copyBounded(position.anchor.data(), position.anchor.size(), anchor.data(), std::min(anchor.size(), MAX_ANCHOR_BYTES));
}

void setXpath(CompactPosition& position, const std::string& xpath) {
  copyBounded(position.xpath.data(), position.xpath.size(), xpath.data(), std::min(xpath.size(), MAX_XPATH_BYTES));
}

uint32_t percentageToQ(const float percentage) {
  const float clamped = std::max(0.0f, std::min(1.0f, percentage));
  return static_cast<uint32_t>(std::lround(clamped * static_cast<float>(PERCENTAGE_SCALE)));
}

float percentageFromQ(const uint32_t percentageQ) {
  return static_cast<float>(std::min(percentageQ, PERCENTAGE_SCALE)) / static_cast<float>(PERCENTAGE_SCALE);
}

bool encodePacket(const PacketType type, const uint8_t* deviceMac, const CompactPosition& position,
                  const std::string& deviceName, uint8_t* output, const size_t capacity, size_t& outputLength) {
  if (!output || !deviceMac || capacity < PACKET_HEADER_BYTES) return false;

  uint8_t* payload = output + PACKET_HEADER_BYTES;
  const size_t payloadCapacity = std::min(capacity, MAX_PACKET_BYTES) - PACKET_HEADER_BYTES;
  size_t payloadLength = 0;

  switch (type) {
    case PacketType::HELLO:
      if (payloadCapacity < DOCUMENT_HASH_BYTES) return false;
      if (boundedLength(position.documentHash.data(), DOCUMENT_HASH_BYTES) != DOCUMENT_HASH_BYTES) return false;
      std::memcpy(payload, position.documentHash.data(), DOCUMENT_HASH_BYTES);
      payloadLength = DOCUMENT_HASH_BYTES;
      break;
    case PacketType::POSITION:
    case PacketType::APPLY:
      if (!serializePosition(position, payload, payloadCapacity, payloadLength)) return false;
      break;
    case PacketType::NAME: {
      const size_t nameLength = std::min({deviceName.size(), MAX_DEVICE_NAME_BYTES, payloadCapacity});
      if (nameLength == 0) return false;
      std::memcpy(payload, deviceName.data(), nameLength);
      payloadLength = nameLength;
      break;
    }
    case PacketType::ACK:
      payloadLength = 0;
      break;
    default:
      return false;
  }

  std::memcpy(output, MAGIC, sizeof(MAGIC));
  output[4] = PROTOCOL_VERSION;
  output[5] = static_cast<uint8_t>(type);
  output[6] = static_cast<uint8_t>(payloadLength & 0xFF);
  output[7] = static_cast<uint8_t>((payloadLength >> 8) & 0xFF);
  std::memcpy(output + 8, deviceMac, MAC_BYTES);

  outputLength = PACKET_HEADER_BYTES + payloadLength;
  return true;
}

bool decodePacket(const uint8_t* data, const size_t length, PacketView& output) {
  if (!data || length < PACKET_HEADER_BYTES || length > MAX_PACKET_BYTES) return false;
  if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) return false;
  if (data[4] != PROTOCOL_VERSION) return false;

  PacketType type;
  if (!isKnownType(data[5], type)) return false;

  const size_t payloadLength = static_cast<size_t>(data[6]) | (static_cast<size_t>(data[7]) << 8);
  if (PACKET_HEADER_BYTES + payloadLength != length) return false;

  const uint8_t* payload = data + PACKET_HEADER_BYTES;
  PacketView parsed;
  parsed.type = type;
  std::memcpy(parsed.deviceMac.data(), data + 8, MAC_BYTES);

  switch (type) {
    case PacketType::HELLO:
      if (payloadLength != DOCUMENT_HASH_BYTES) return false;
      copyBounded(parsed.position.documentHash.data(), parsed.position.documentHash.size(),
                  reinterpret_cast<const char*>(payload), DOCUMENT_HASH_BYTES);
      break;
    case PacketType::POSITION:
    case PacketType::APPLY:
      if (!parsePosition(payload, payloadLength, parsed.position)) return false;
      break;
    case PacketType::NAME:
      if (payloadLength == 0 || payloadLength > MAX_DEVICE_NAME_BYTES) return false;
      parsed.deviceName.assign(reinterpret_cast<const char*>(payload), payloadLength);
      break;
    case PacketType::ACK:
      if (payloadLength != 0) return false;
      break;
  }

  output = std::move(parsed);
  return true;
}

}  // namespace nearby_position
