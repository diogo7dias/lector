#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "BitmapHelpers.h"
#include "BmpWriter.h"

// Streaming 2-bit BMP downsampler for library cover thumbnails (132x196).
// Reads one source row at a time and area-averages into the target width, so
// peak RAM stays ~2 KB (safe on ESP32-C3 with no PSRAM).
namespace thumb_resampler {

constexpr int THUMB_WIDTH = 132;
constexpr int THUMB_HEIGHT = 196;

inline uint16_t loadLE16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }

inline uint32_t loadLE32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Downsamples a 2-bit uncompressed BMP read from `src` (must support seekSet and
// read(void*, size_t)) into a top-down 2-bit BMP written to `dst` (Print), fitting
// inside targetW x targetH while preserving aspect ratio.
template <typename SrcFile, typename DstPrint>
bool downsample2BitBmp(SrcFile& src, DstPrint& dst, const int targetW = THUMB_WIDTH, const int targetH = THUMB_HEIGHT) {
  uint8_t hdr[54];
  if (!src.seekSet(0) || src.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) return false;
  if (hdr[0] != 'B' || hdr[1] != 'M') return false;

  const uint32_t pixelOffset = loadLE32(hdr + 10);
  const uint32_t dibSize = loadLE32(hdr + 14);
  if (dibSize < 40 || pixelOffset < 54) return false;

  const int32_t rawW = static_cast<int32_t>(loadLE32(hdr + 18));
  const int32_t rawH = static_cast<int32_t>(loadLE32(hdr + 22));
  const uint16_t planes = loadLE16(hdr + 26);
  const uint16_t bpp = loadLE16(hdr + 28);
  const uint32_t compression = loadLE32(hdr + 30);
  if (planes != 1 || bpp != 2 || compression != 0) return false;

  const bool topDown = rawH < 0;
  const int srcW = rawW;
  const int srcH = topDown ? -rawH : rawH;
  if (srcW <= 0 || srcH <= 0 || srcW > 2048 || srcH > 3072) return false;

  int outW = srcW;
  int outH = srcH;
  if (srcW > targetW || srcH > targetH) {
    scaleToFit(srcW, srcH, targetW, targetH, /*crop=*/false, outW, outH);
  }

  const int srcRowBytes = (srcW * 2 + 31) / 32 * 4;
  const int outRowBytes = (outW * 2 + 31) / 32 * 4;
  if (srcRowBytes <= 0 || outRowBytes <= 0) return false;

  auto srcPacked = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(srcRowBytes));
  auto srcGray = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(srcW));
  auto rowAccum = makeUniqueNoThrow<uint32_t[]>(static_cast<size_t>(outW));
  auto rowCount = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(outW));
  auto outRow = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(outRowBytes));
  if (!srcPacked || !srcGray || !rowAccum || !rowCount || !outRow) return false;

  AtkinsonDitherer ditherer(outW, Gray4QuantizationMode::Native);
  if (!ditherer.valid()) return false;

  constexpr uint8_t kPaletteGray[4] = {0, 85, 170, 255};
  if (bmp_writer::writeHeader(dst, outW, outH, 2, /*topDown=*/true) == 0) return false;

  const uint32_t scaleX_fp = (static_cast<uint32_t>(srcW) << 16) / static_cast<uint32_t>(outW);
  for (int outY = 0; outY < outH; ++outY) {
    const int y0 = (outY * srcH) / outH;
    int y1 = ((outY + 1) * srcH) / outH;
    if (y1 <= y0) y1 = std::min(srcH, y0 + 1);

    memset(rowAccum.get(), 0, static_cast<size_t>(outW) * sizeof(uint32_t));
    memset(rowCount.get(), 0, static_cast<size_t>(outW) * sizeof(uint16_t));

    for (int srcY = y0; srcY < y1; ++srcY) {
      const int fileRow = topDown ? srcY : (srcH - 1 - srcY);
      const uint32_t off = pixelOffset + static_cast<uint32_t>(fileRow) * static_cast<uint32_t>(srcRowBytes);
      if (!src.seekSet(off) || src.read(srcPacked.get(), srcRowBytes) != srcRowBytes) return false;
      for (int x = 0; x < srcW; ++x) {
        const uint8_t idx = (srcPacked[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03;
        srcGray[x] = kPaletteGray[idx];
      }
      accumulateAreaRow(srcGray.get(), srcW, outW, scaleX_fp, rowAccum.get(), rowCount.get());
    }

    packBmpRow(outRow.get(), outRowBytes, outW, ditherer, [&](const int x) -> uint8_t {
      const uint16_t cnt = rowCount[x];
      return cnt > 0 ? static_cast<uint8_t>(rowAccum[x] / cnt) : 255;
    });
    if (dst.write(outRow.get(), static_cast<size_t>(outRowBytes)) != static_cast<size_t>(outRowBytes)) return false;
  }
  return true;
}

}  // namespace thumb_resampler
