#pragma once

#include <cstddef>
#include <cstdint>

// Pure bit-transform from a 2bpp .pxc payload to the three 1bpp framebuffer
// planes the sleep-screen grayscale pipeline displays (BW silhouette base,
// GRAYSCALE_LSB, GRAYSCALE_MSB). This reproduces exactly what
// renderPxcSleepScreen produces through DirectPixelWriter in Portrait
// orientation — the only orientation the sleep screen renders in — so the
// idle prestager can build the planes on SD without touching the live
// framebuffer, and the lock path can stream them straight in.
//
// Pixel rules (mirrors DirectPixelWriter::writePixel; pixel values 0..3,
// 0 = black, 3 = white):
//   BW base (buffer starts 0xFF/white): clear bit (black) when value < 3.
//     Fast (1-bit) mode first collapses mid tones with the 2x2 Bayer dither
//     renderPxcSleepScreen uses, then applies the same rule.
//   LSB (buffer starts 0x00): set bit when value == 1.
//   MSB (buffer starts 0x00): set bit when value == 1 or 2.
//
// Portrait mapping (DirectPixelWriter, logical (col,row) -> physical):
//   phyX = row, phyY = phyHeight - 1 - col
// so one logical pxc COLUMN becomes one physical framebuffer ROW. Packing a
// band of physical rows therefore consumes a contiguous range of logical
// columns from every payload row — the packer streams payload rows and
// scatters bits into a band-sized scratch, keeping RAM use at
// bandRows * strideBytes per plane.
namespace pxc_plane {

enum class Plane : uint8_t { BwBase, BwFastDithered, Lsb, Msb };

struct Geometry {
  uint16_t logicalWidth;    // pxc width  (columns per payload row)
  uint16_t logicalHeight;   // pxc height (payload rows)
  uint16_t strideBytes;     // physical framebuffer row stride
  uint16_t physicalHeight;  // physical framebuffer rows (== logicalWidth)
};

// The byte each plane's scratch must be initialised with before packing.
inline uint8_t planeFill(const Plane plane) {
  return (plane == Plane::BwBase || plane == Plane::BwFastDithered) ? 0xFF : 0x00;
}

// 2bpp pixel fetch from one payload row (4 px/byte, MSB first).
inline uint8_t pixelAt(const uint8_t* payloadRow, const int col) {
  return (payloadRow[col >> 2] >> (6 - (col & 3) * 2)) & 0x03;
}

// Whether this plane draws (flips its fill bit) for the given pixel value at
// logical (col,row). Bayer indices mirror renderPxcSleepScreen's fast path.
inline bool planeDraws(const Plane plane, uint8_t value, const int col, const int row) {
  switch (plane) {
    case Plane::BwFastDithered:
      if (value != 0 && value != 3) {
        static const uint8_t kBayer2[2][2] = {{0, 2}, {3, 1}};
        value = (value > kBayer2[row & 1][col & 1]) ? 3 : 0;
      }
      return value < 3;
    case Plane::BwBase:
      return value < 3;
    case Plane::Lsb:
      return value == 1;
    case Plane::Msb:
      return value == 1 || value == 2;
  }
  return false;
}

// Scatter one payload row's contribution into a band scratch.
//   scratch     : bandRows * strideBytes bytes, pre-filled with planeFill()
//   payloadRow  : one 2bpp row (geometry.logicalWidth pixels)
//   row         : logical row index of payloadRow
//   bandY0      : first physical row of the band
//   bandRows    : physical rows in the band
// Physical row sy (bandY0 <= bandY0+sy < physicalHeight) shows logical column
// col = physicalHeight - 1 - (bandY0 + sy); this row's pixel for that column
// lands at physical x = row.
inline void packRowIntoBand(const Plane plane, const Geometry& g, const uint8_t* payloadRow, const int row,
                            const int bandY0, const int bandRows, uint8_t* scratch) {
  const int byteX = row >> 3;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (7 - (row & 7)));
  const bool blackens = (plane == Plane::BwBase || plane == Plane::BwFastDithered);
  for (int sy = 0; sy < bandRows; sy++) {
    const int phyY = bandY0 + sy;
    const int col = g.physicalHeight - 1 - phyY;
    if (col < 0 || col >= g.logicalWidth) continue;
    if (!planeDraws(plane, pixelAt(payloadRow, col), col, row)) continue;
    uint8_t& b = scratch[static_cast<size_t>(sy) * g.strideBytes + byteX];
    if (blackens) {
      b = static_cast<uint8_t>(b & ~bitMask);  // draw black (clear bit)
    } else {
      b = static_cast<uint8_t>(b | bitMask);  // draw white (set bit)
    }
  }
}

}  // namespace pxc_plane
