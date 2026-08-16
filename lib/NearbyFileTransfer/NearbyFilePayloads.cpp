#include "NearbyFilePayloads.h"

#include <algorithm>
#include <cstring>

namespace nearby_file {
namespace {

void writeU64(uint8_t*& cursor, const uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) *cursor++ = static_cast<uint8_t>((value >> shift) & 0xFF);
}

void writeU32(uint8_t*& cursor, const uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) *cursor++ = static_cast<uint8_t>((value >> shift) & 0xFF);
}

bool readU64(const uint8_t*& cursor, size_t& remaining, uint64_t& value) {
  if (remaining < 8) return false;
  value = 0;
  for (int index = 0; index < 8; index++) value |= static_cast<uint64_t>(cursor[index]) << (index * 8);
  cursor += 8;
  remaining -= 8;
  return true;
}

bool readU32(const uint8_t*& cursor, size_t& remaining, uint32_t& value) {
  if (remaining < 4) return false;
  value = 0;
  for (int index = 0; index < 4; index++) value |= static_cast<uint32_t>(cursor[index]) << (index * 8);
  cursor += 4;
  remaining -= 4;
  return true;
}

/** Writes a byte-length-prefixed string, truncated to `maxBytes`. */
bool writeString(uint8_t*& cursor, const uint8_t* end, const std::string& text, const size_t maxBytes) {
  const size_t length = std::min(text.size(), maxBytes);
  if (static_cast<size_t>(end - cursor) < length + 1) return false;
  *cursor++ = static_cast<uint8_t>(length);
  if (length > 0) std::memcpy(cursor, text.data(), length);
  cursor += length;
  return true;
}

bool readString(const uint8_t*& cursor, size_t& remaining, const size_t maxBytes, std::string& text) {
  if (remaining < 1) return false;
  const uint8_t length = *cursor++;
  remaining--;
  // The length byte is sender-controlled: refuse one that overruns the packet or
  // exceeds what this field is allowed to hold.
  if (length > maxBytes || remaining < length) return false;
  text.assign(reinterpret_cast<const char*>(cursor), length);
  cursor += length;
  remaining -= length;
  return true;
}

}  // namespace

bool encodeNamePayload(const std::string& deviceName, uint8_t* output, const size_t capacity, size_t& outputLength) {
  if (!output) return false;
  uint8_t* cursor = output;
  if (!writeString(cursor, output + capacity, deviceName, MAX_NAME_BYTES)) return false;
  outputLength = static_cast<size_t>(cursor - output);
  return true;
}

bool decodeNamePayload(const uint8_t* data, const size_t length, std::string& deviceName) {
  if (!data) return false;
  size_t remaining = length;
  return readString(data, remaining, MAX_NAME_BYTES, deviceName);
}

bool encodeOfferPayload(const OfferPayload& offer, uint8_t* output, const size_t capacity, size_t& outputLength) {
  if (!output || capacity < 8) return false;
  uint8_t* cursor = output;
  const uint8_t* end = output + capacity;

  writeU64(cursor, offer.fileSize);
  if (!writeString(cursor, end, offer.deviceName, MAX_NAME_BYTES)) return false;
  if (!writeString(cursor, end, offer.fileName, MAX_OFFER_NAME_BYTES)) return false;

  outputLength = static_cast<size_t>(cursor - output);
  return true;
}

bool decodeOfferPayload(const uint8_t* data, const size_t length, OfferPayload& offer) {
  if (!data) return false;
  size_t remaining = length;
  if (!readU64(data, remaining, offer.fileSize)) return false;
  if (!readString(data, remaining, MAX_NAME_BYTES, offer.deviceName)) return false;
  return readString(data, remaining, MAX_OFFER_NAME_BYTES, offer.fileName);
}

bool encodeCompletePayload(const CompletePayload& complete, uint8_t* output, const size_t capacity,
                           size_t& outputLength) {
  if (!output || capacity < 12) return false;
  uint8_t* cursor = output;
  writeU32(cursor, complete.crc32);
  writeU64(cursor, complete.totalBytes);
  outputLength = static_cast<size_t>(cursor - output);
  return true;
}

bool decodeCompletePayload(const uint8_t* data, const size_t length, CompletePayload& complete) {
  if (!data) return false;
  size_t remaining = length;
  if (!readU32(data, remaining, complete.crc32)) return false;
  return readU64(data, remaining, complete.totalBytes);
}

bool encodeResultPayload(const bool success, uint8_t* output, const size_t capacity, size_t& outputLength) {
  if (!output || capacity < 1) return false;
  output[0] = success ? 1 : 0;
  outputLength = 1;
  return true;
}

bool decodeResultPayload(const uint8_t* data, const size_t length, bool& success) {
  if (!data || length < 1) return false;
  success = data[0] != 0;
  return true;
}

}  // namespace nearby_file
