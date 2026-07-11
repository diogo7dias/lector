#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace font_upload {
inline constexpr size_t MAX_FAMILY_NAME_BYTES = 63;
inline constexpr size_t MAX_FILENAME_BYTES = 120;
inline constexpr size_t MAX_FILE_BYTES = 64 * 1024 * 1024;
inline constexpr size_t MAX_FAMILIES = 128;
inline constexpr size_t MAX_RUNTIME_PATH_BYTES = 127;

inline constexpr bool lengthsAreSafe(const size_t familyLength, const size_t filenameLength) {
  return familyLength > 0 && familyLength <= MAX_FAMILY_NAME_BYTES && filenameLength > 0 &&
         filenameLength <= MAX_FILENAME_BYTES;
}

inline constexpr bool canInstallFamily(const size_t familyCount, const bool alreadyInstalled) {
  return alreadyInstalled || familyCount < MAX_FAMILIES;
}

inline constexpr bool pathLengthIsSafe(const size_t pathLength) { return pathLength <= MAX_RUNTIME_PATH_BYTES; }

inline bool glyphRecordIsSafe(const uint8_t* glyph, const bool is2Bit, const uint32_t expectedOffset,
                              const uint32_t bitmapBytesAvailable) {
  if (!glyph || expectedOffset > bitmapBytesAvailable) return false;
  const uint32_t pixels = static_cast<uint32_t>(glyph[0]) * glyph[1];
  const uint32_t expectedLength = (pixels * (is2Bit ? 2u : 1u) + 7u) / 8u;
  const uint32_t dataLength = static_cast<uint16_t>(glyph[8]) | (static_cast<uint16_t>(glyph[9]) << 8);
  const uint32_t dataOffset = static_cast<uint32_t>(glyph[12]) | (static_cast<uint32_t>(glyph[13]) << 8) |
                              (static_cast<uint32_t>(glyph[14]) << 16) | (static_cast<uint32_t>(glyph[15]) << 24);
  return dataLength == expectedLength && dataOffset == expectedOffset &&
         dataLength <= bitmapBytesAvailable - expectedOffset;
}
}  // namespace font_upload

class FontUploadPolicy {
 public:
  bool add(const uint8_t* data, const size_t length) {
    if (data == nullptr || length > std::numeric_limits<size_t>::max() - receivedBytes_) {
      valid_ = false;
      return false;
    }
    const size_t copyBytes = std::min(length, prefix_.size() - prefixBytes_);
    if (copyBytes > 0) {
      std::memcpy(prefix_.data() + prefixBytes_, data, copyBytes);
      prefixBytes_ += copyBytes;
    }
    receivedBytes_ += length;
    if (receivedBytes_ > font_upload::MAX_FILE_BYTES) valid_ = false;
    return valid_;
  }

  bool finish(const size_t expectedBytes) const {
    static constexpr uint8_t MAGIC[] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};
    if (!valid_ || expectedBytes != receivedBytes_ || receivedBytes_ < HEADER_SIZE ||
        expectedBytes > font_upload::MAX_FILE_BYTES) {
      return false;
    }
    const uint16_t version = readU16(prefix_.data() + 8);
    const uint8_t styleCount = prefix_[12];
    if (std::memcmp(prefix_.data(), MAGIC, sizeof(MAGIC)) != 0 || version != 4 || styleCount < 1 || styleCount > 4) {
      return false;
    }
    const size_t tocEnd = HEADER_SIZE + static_cast<size_t>(styleCount) * TOC_SIZE;
    if (prefixBytes_ < tocEnd || expectedBytes < tocEnd) return false;

    uint8_t seenStyles = 0;
    for (uint8_t i = 0; i < styleCount; ++i) {
      const uint8_t* toc = prefix_.data() + HEADER_SIZE + static_cast<size_t>(i) * TOC_SIZE;
      const uint8_t styleId = toc[0];
      if (styleId >= 4 || (seenStyles & (1u << styleId)) != 0) return false;
      seenStyles |= static_cast<uint8_t>(1u << styleId);
      const uint32_t intervals = readU32(toc + 4);
      const uint32_t glyphs = readU32(toc + 8);
      const uint32_t kernLeft = readU16(toc + 17);
      const uint32_t kernRight = readU16(toc + 19);
      if (intervals == 0 || intervals > 4096 || glyphs == 0 || glyphs > 65536 || kernLeft > 4096 ||
          kernRight > 4096) {
        return false;
      }
      size_t fixedBytes = 0;
      if (!checkedAddProduct(fixedBytes, intervals, 12) || !checkedAddProduct(fixedBytes, glyphs, 16) ||
          !checkedAddProduct(fixedBytes, kernLeft, 3) || !checkedAddProduct(fixedBytes, kernRight, 3) ||
          !checkedAddProduct(fixedBytes, toc[21], toc[22]) || !checkedAddProduct(fixedBytes, toc[23], 8)) {
        return false;
      }
      const size_t dataOffset = readU32(toc + 24);
      if (dataOffset < tocEnd || dataOffset > expectedBytes || fixedBytes > expectedBytes - dataOffset) return false;
    }
    return true;
  }

  size_t receivedBytes() const { return receivedBytes_; }

 private:
  static constexpr size_t HEADER_SIZE = 32;
  static constexpr size_t TOC_SIZE = 32;
  static uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  }
  static uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }
  static bool checkedAddProduct(size_t& total, const size_t count, const size_t itemSize) {
    if (count != 0 && itemSize > std::numeric_limits<size_t>::max() / count) return false;
    const size_t bytes = count * itemSize;
    if (bytes > std::numeric_limits<size_t>::max() - total) return false;
    total += bytes;
    return true;
  }

  std::array<uint8_t, HEADER_SIZE + 4 * TOC_SIZE> prefix_{};
  size_t prefixBytes_ = 0;
  size_t receivedBytes_ = 0;
  bool valid_ = true;
};
