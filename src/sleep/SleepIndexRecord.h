#pragma once

// Fixed-size record layout for /.crosspoint/sleep_index.bin (version 2).
//
// Each 160-byte record carries the wallpaper's basename (NUL-terminated in bytes
// 0..155) and its FAT directory byte offset (little-endian uint32_t in bytes
// 156..159). Storing the directory offset lets lock-time wallpaper opening seek
// directly to the FAT slot in O(1) instead of running a linear name lookup across
// thousands of entries.
//
// Pure header (no SD/ESP headers) so host unit tests exercise the exact encoding
// and candidate-matching logic used by firmware.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace sleep_index_record {

inline constexpr size_t kRecordBytes = 160;
inline constexpr size_t kDirOffsetBytes = sizeof(uint32_t);
inline constexpr size_t kDirOffsetFieldPos = kRecordBytes - kDirOffsetBytes;
// Bytes 0..154 hold up to 155 name characters; byte 155 is always '\0'.
inline constexpr size_t kMaxNameBytes = kDirOffsetFieldPos - 1;
inline constexpr uint32_t kUnknownDirOffset = std::numeric_limits<uint32_t>::max();

inline void encode(char (&record)[kRecordBytes], const char* const name, const size_t len, const uint32_t dirOffset) {
  std::memset(record, 0, kRecordBytes);
  const size_t copyLen = len > kMaxNameBytes ? kMaxNameBytes : len;
  if (copyLen > 0 && name != nullptr) {
    std::memcpy(record, name, copyLen);
  }
  record[kDirOffsetFieldPos] = static_cast<char>(dirOffset & 0xFFu);
  record[kDirOffsetFieldPos + 1] = static_cast<char>((dirOffset >> 8) & 0xFFu);
  record[kDirOffsetFieldPos + 2] = static_cast<char>((dirOffset >> 16) & 0xFFu);
  record[kDirOffsetFieldPos + 3] = static_cast<char>((dirOffset >> 24) & 0xFFu);
}

inline std::string_view decodeName(const char* const record) {
  if (record == nullptr) return {};
  const size_t len = strnlen(record, kMaxNameBytes);
  return std::string_view(record, len);
}

inline uint32_t decodeDirOffset(const char* const record) {
  if (record == nullptr) return kUnknownDirOffset;
  const auto b0 = static_cast<uint8_t>(record[kDirOffsetFieldPos]);
  const auto b1 = static_cast<uint8_t>(record[kDirOffsetFieldPos + 1]);
  const auto b2 = static_cast<uint8_t>(record[kDirOffsetFieldPos + 2]);
  const auto b3 = static_cast<uint8_t>(record[kDirOffsetFieldPos + 3]);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

// True when the entry opened at a cached FAT directory offset matches either the
// expected basename or its favorite-renamed counterpart.
inline bool matchesCandidate(const std::string_view actual, const std::string_view expected,
                             const std::string_view counterpart = {}) {
  if (actual.empty()) return false;
  if (!expected.empty() && actual == expected) return true;
  if (!counterpart.empty() && actual == counterpart) return true;
  return false;
}

}  // namespace sleep_index_record
