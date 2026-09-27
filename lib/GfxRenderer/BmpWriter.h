#pragma once

#include <Print.h>

#include <cstdint>

// The BMP file header the image converters emit. Both the PNG and the JPEG
// converter wrote byte-for-byte the same 1-bit and 2-bit headers; this is that
// code, once. Rows default to top-down (negative height), which is what the
// converters stream; the screenshot writer asks for bottom-up.
namespace bmp_writer {

inline size_t write16(Print& out, const uint16_t value) {
  size_t n = out.write(value & 0xFF);
  n += out.write((value >> 8) & 0xFF);
  return n;
}

inline size_t write32(Print& out, const uint32_t value) {
  size_t n = write16(out, value & 0xFFFF);
  n += write16(out, value >> 16);
  return n;
}

// bitsPerPixel is 1 or 2; the palette is the matching even ramp from black to
// white, so a 2-bit file gets 0 / 85 / 170 / 255. Returns the bytes written, so
// a caller can detect a short write.
inline size_t writeHeader(Print& out, const int width, const int height, const uint8_t bitsPerPixel,
                          const bool topDown = true) {
  const int colors = 1 << bitsPerPixel;
  const int bytesPerRow = (width * bitsPerPixel + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t pixelOffset = 14 + 40 + static_cast<uint32_t>(colors) * 4;

  // File header (14 bytes)
  size_t n = out.write('B');
  n += out.write('M');
  n += write32(out, pixelOffset + imageSize);
  n += write32(out, 0);
  n += write32(out, pixelOffset);

  // BITMAPINFOHEADER (40 bytes)
  n += write32(out, 40);
  n += write32(out, static_cast<uint32_t>(width));
  n += write32(out, static_cast<uint32_t>(topDown ? -height : height));  // negative height = top-down rows
  n += write16(out, 1);                                                  // colour planes
  n += write16(out, bitsPerPixel);
  n += write32(out, 0);  // BI_RGB, no compression
  n += write32(out, imageSize);
  n += write32(out, 2835);  // xPixelsPerMeter (72 DPI)
  n += write32(out, 2835);  // yPixelsPerMeter (72 DPI)
  n += write32(out, colors);
  n += write32(out, colors);

  // Palette, BGRA per entry
  for (int i = 0; i < colors; i++) {
    const auto level = static_cast<uint8_t>(i * 255 / (colors - 1));
    n += out.write(level);
    n += out.write(level);
    n += out.write(level);
    n += out.write(static_cast<uint8_t>(0));
  }
  return n;
}

inline void writeHeader1bit(Print& out, const int width, const int height) { writeHeader(out, width, height, 1); }
inline void writeHeader2bit(Print& out, const int width, const int height) { writeHeader(out, width, height, 2); }

}  // namespace bmp_writer
