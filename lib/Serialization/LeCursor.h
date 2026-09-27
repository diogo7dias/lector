#pragma once

#include <NearbyTransfer.h>

#include <cstddef>
#include <cstdint>

// Little-endian fields read and written through a moving cursor, on top of the SDK's
// fixed-offset readU*/writeU*. Writers trust the caller to have checked the room; readers
// refuse a short buffer and leave the cursor where it was.
namespace le {

inline void writeU16(uint8_t*& cursor, const uint16_t value) {
  freeink::nearby::writeU16(cursor, value);
  cursor += 2;
}

inline void writeU32(uint8_t*& cursor, const uint32_t value) {
  freeink::nearby::writeU32(cursor, value);
  cursor += 4;
}

inline void writeU64(uint8_t*& cursor, const uint64_t value) {
  freeink::nearby::writeU64(cursor, value);
  cursor += 8;
}

inline bool readU16(const uint8_t*& cursor, size_t& remaining, uint16_t& value) {
  if (remaining < 2) return false;
  value = freeink::nearby::readU16(cursor);
  cursor += 2;
  remaining -= 2;
  return true;
}

inline bool readU32(const uint8_t*& cursor, size_t& remaining, uint32_t& value) {
  if (remaining < 4) return false;
  value = freeink::nearby::readU32(cursor);
  cursor += 4;
  remaining -= 4;
  return true;
}

inline bool readU64(const uint8_t*& cursor, size_t& remaining, uint64_t& value) {
  if (remaining < 8) return false;
  value = freeink::nearby::readU64(cursor);
  cursor += 8;
  remaining -= 8;
  return true;
}

}  // namespace le
