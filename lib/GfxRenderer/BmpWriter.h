#pragma once

#include <Print.h>

#include <cstdint>

// The BMP file header the image converters emit. Both the PNG and the JPEG
// converter wrote byte-for-byte the same 1-bit and 2-bit headers; this is that
// code, once. Rows are written top-down (negative height), which is what the
// converters stream.
namespace bmp_writer {

inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) { write32(out, static_cast<uint32_t>(value)); }

// bitsPerPixel is 1 or 2; the palette is the matching even ramp from black to
// white, so a 2-bit file gets 0 / 85 / 170 / 255.
inline void writeHeader(Print& out, const int width, const int height, const uint8_t bitsPerPixel) {
  const int colors = 1 << bitsPerPixel;
  const int bytesPerRow = (width * bitsPerPixel + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t pixelOffset = 14 + 40 + static_cast<uint32_t>(colors) * 4;

  // File header (14 bytes)
  out.write('B');
  out.write('M');
  write32(out, pixelOffset + imageSize);
  write32(out, 0);
  write32(out, pixelOffset);

  // BITMAPINFOHEADER (40 bytes)
  write32(out, 40);
  write32Signed(out, width);
  write32Signed(out, -height);  // negative height = top-down rows
  write16(out, 1);              // colour planes
  write16(out, bitsPerPixel);
  write32(out, 0);  // BI_RGB, no compression
  write32(out, imageSize);
  write32(out, 2835);  // xPixelsPerMeter (72 DPI)
  write32(out, 2835);  // yPixelsPerMeter (72 DPI)
  write32(out, colors);
  write32(out, colors);

  // Palette, BGRA per entry
  for (int i = 0; i < colors; i++) {
    const auto level = static_cast<uint8_t>(i * 255 / (colors - 1));
    out.write(level);
    out.write(level);
    out.write(level);
    out.write(static_cast<uint8_t>(0));
  }
}

inline void writeHeader1bit(Print& out, const int width, const int height) { writeHeader(out, width, height, 1); }
inline void writeHeader2bit(Print& out, const int width, const int height) { writeHeader(out, width, height, 2); }

}  // namespace bmp_writer
