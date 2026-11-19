#pragma once

#include <HalStorage.h>
#include <Logging.h>
#include <Print.h>

#include <cstdint>
#include <cstring>

// What the GIF paths share: the header walk that finds the first image descriptor (canvas
// size, interlace flag) and the cap on a GIF pulled out of a book's zip.
struct GifBasicInfo {
  uint16_t canvasWidth{0};
  uint16_t canvasHeight{0};
  uint16_t frameX{0};
  uint16_t frameY{0};
  uint16_t frameWidth{0};
  uint16_t frameHeight{0};
  bool interlaced{false};
};

namespace GifCommon {

// A cover is unzipped to the SD card before it is decoded; a zip entry that lies about its
// size stops here instead of filling the card.
constexpr uint32_t MAX_FILE_BYTES = 16 * 1024 * 1024;

// Extension blocks before the first image. Real files carry a handful (graphic control,
// comment, NETSCAPE loop); each is a seek on the SD card, so a hostile file made of nothing
// else must stop long before the end of a 16 MB entry.
constexpr int MAX_HEADER_BLOCKS = 256;

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

inline bool skipSubBlocks(HalFile& file) {
  uint8_t blockLen = 0;
  for (int i = 0; i < MAX_HEADER_BLOCKS * 16; i++) {
    if (file.read(&blockLen, 1) != 1) {
      LOG_ERR("GIF", "Failed to read GIF sub-block length");
      return false;
    }
    if (blockLen == 0) return true;
    if (!file.seekCur(blockLen)) {
      LOG_ERR("GIF", "Failed to skip GIF sub-block (%u bytes)", blockLen);
      return false;
    }
  }
  LOG_ERR("GIF", "GIF extension has too many sub-blocks");
  return false;
}

// Leaves the file at offset 0 on success.
inline bool readBasicInfo(HalFile& file, GifBasicInfo& info) {
  info = {};
  const auto fail = [&file](const char* why) {
    LOG_ERR("GIF", "%s", why);
    file.seek(0);
    return false;
  };
  if (!file.seek(0)) return fail("Failed to seek to GIF header");

  uint8_t header[13];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return fail("Failed to read GIF header");
  if (memcmp(header, "GIF87a", 6) != 0 && memcmp(header, "GIF89a", 6) != 0) return fail("Invalid GIF signature");

  info.canvasWidth = readLe16(&header[6]);
  info.canvasHeight = readLe16(&header[8]);
  const uint8_t packed = header[10];
  if ((packed & 0x80) != 0) {
    const uint32_t paletteEntries = 1u << ((packed & 0x07) + 1u);
    if (!file.seekCur(static_cast<int32_t>(paletteEntries * 3u))) return fail("Failed to skip GIF global color table");
  }

  for (int block = 0; block < MAX_HEADER_BLOCKS; block++) {
    uint8_t marker = 0;
    if (file.read(&marker, 1) != 1) return fail("Unexpected EOF before GIF image descriptor");
    if (marker == 0x3B) return fail("GIF trailer reached before image descriptor");

    if (marker == 0x21) {
      uint8_t label = 0;
      if (file.read(&label, 1) != 1) return fail("Failed to read GIF extension label");
      if (!skipSubBlocks(file)) return fail("Failed to skip GIF extension data");
      continue;
    }

    if (marker != 0x2C) {
      LOG_ERR("GIF", "Unexpected GIF block marker 0x%02X", marker);
      return fail("Unsupported GIF block");
    }

    uint8_t descriptor[9];
    if (file.read(descriptor, sizeof(descriptor)) != static_cast<int>(sizeof(descriptor))) {
      return fail("Failed to read GIF image descriptor");
    }
    info.frameX = readLe16(&descriptor[0]);
    info.frameY = readLe16(&descriptor[2]);
    info.frameWidth = readLe16(&descriptor[4]);
    info.frameHeight = readLe16(&descriptor[6]);
    info.interlaced = (descriptor[8] & 0x40) != 0;
    if ((descriptor[8] & 0x80) != 0) {
      const uint32_t paletteBytes = 3u << ((descriptor[8] & 0x07) + 1u);
      if (!file.seekCur(paletteBytes)) return fail("Failed to skip GIF local color table");
    }
    uint8_t codeSize = 0;
    if (file.read(&codeSize, 1) != 1 || codeSize < 2 || codeSize > 8) {
      return fail("Invalid or missing LZW minimum code size");
    }
    return file.seek(0);
  }
  return fail("GIF has too many blocks before the image");
}

}  // namespace GifCommon

// A short write stops ZipFile streaming even when the ZIP directory lies about size.
class GifLimitedWriter final : public Print {
  Print& output;
  size_t remaining;

 public:
  GifLimitedWriter(Print& output, size_t limit) : output(output), remaining(limit) {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (size > remaining) return 0;
    const size_t written = output.write(data, size);
    remaining -= written;
    return written;
  }
};
