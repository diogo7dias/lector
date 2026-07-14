#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "sleep/PxcPlanePacker.h"

namespace {

using pxc_plane::Geometry;
using pxc_plane::Plane;

// Independent reference: replays DirectPixelWriter::writePixel semantics for
// the Portrait orientation the sleep screen renders in, pixel by pixel over
// the full frame. The packer must produce byte-identical planes.
//   Portrait: phyX = row, phyY = physicalHeight - 1 - col
//   BW draws black (clear bit) when value < 3, buffer starts 0xFF.
//   LSB draws white (set bit) when value == 1, buffer starts 0x00.
//   MSB draws white (set bit) when value == 1 or 2, buffer starts 0x00.
//   Fast BW first applies the 2x2 Bayer dither from renderPxcSleepScreen.
std::vector<uint8_t> referencePlane(const Plane plane, const Geometry& g, const std::vector<uint8_t>& payload) {
  const int bytesPerRow = (g.logicalWidth + 3) / 4;
  std::vector<uint8_t> fb(static_cast<size_t>(g.strideBytes) * g.physicalHeight, pxc_plane::planeFill(plane));
  for (int row = 0; row < g.logicalHeight; row++) {
    const uint8_t* rowBuffer = payload.data() + static_cast<size_t>(row) * bytesPerRow;
    for (int col = 0; col < g.logicalWidth; col++) {
      uint8_t value = (rowBuffer[col >> 2] >> (6 - (col & 3) * 2)) & 0x03;
      if (plane == Plane::BwFastDithered && value != 0 && value != 3) {
        static const uint8_t kBayer2[2][2] = {{0, 2}, {3, 1}};
        value = (value > kBayer2[row & 1][col & 1]) ? 3 : 0;
      }
      bool draw;
      bool drawsBlack;
      switch (plane) {
        case Plane::BwBase:
        case Plane::BwFastDithered:
          draw = value < 3;
          drawsBlack = true;
          break;
        case Plane::Lsb:
          draw = value == 1;
          drawsBlack = false;
          break;
        case Plane::Msb:
          draw = (value == 1 || value == 2);
          drawsBlack = false;
          break;
      }
      if (!draw) continue;
      const int phyX = row;
      const int phyY = g.physicalHeight - 1 - col;
      const size_t byteIndex = static_cast<size_t>(phyY) * g.strideBytes + (phyX >> 3);
      const uint8_t bitMask = static_cast<uint8_t>(1u << (7 - (phyX & 7)));
      if (drawsBlack) {
        fb[byteIndex] = static_cast<uint8_t>(fb[byteIndex] & ~bitMask);
      } else {
        fb[byteIndex] = static_cast<uint8_t>(fb[byteIndex] | bitMask);
      }
    }
  }
  return fb;
}

// Assemble the full plane by running the band packer the way the prestager
// does: bands of physical rows, streaming every payload row into each band.
std::vector<uint8_t> packedPlane(const Plane plane, const Geometry& g, const std::vector<uint8_t>& payload,
                                 const int bandRows) {
  const int bytesPerRow = (g.logicalWidth + 3) / 4;
  std::vector<uint8_t> fb(static_cast<size_t>(g.strideBytes) * g.physicalHeight, 0xAA);  // poison
  std::vector<uint8_t> scratch;
  for (int bandY0 = 0; bandY0 < g.physicalHeight; bandY0 += bandRows) {
    const int rows = std::min<int>(bandRows, g.physicalHeight - bandY0);
    scratch.assign(static_cast<size_t>(rows) * g.strideBytes, pxc_plane::planeFill(plane));
    for (int row = 0; row < g.logicalHeight; row++) {
      const uint8_t* rowBuffer = payload.data() + static_cast<size_t>(row) * bytesPerRow;
      pxc_plane::packRowIntoBand(plane, g, rowBuffer, row, bandY0, rows, scratch.data());
    }
    std::memcpy(fb.data() + static_cast<size_t>(bandY0) * g.strideBytes, scratch.data(), scratch.size());
  }
  return fb;
}

std::vector<uint8_t> randomPayload(const Geometry& g, const uint32_t seed) {
  const int bytesPerRow = (g.logicalWidth + 3) / 4;
  std::vector<uint8_t> payload(static_cast<size_t>(bytesPerRow) * g.logicalHeight);
  std::mt19937 rng(seed);
  for (auto& b : payload) b = static_cast<uint8_t>(rng());
  return payload;
}

// Small geometry that still exercises non-multiple-of-8 stride edges and a
// band size that does not divide the height evenly.
constexpr Geometry kSmall{20, 24, 3, 20};
// X3 sleep-screen geometry: 528x792 logical, 99-byte stride, 528 physical rows.
constexpr Geometry kX3{528, 792, 99, 528};
// X4: 480x800 logical, 100-byte stride, 480 physical rows.
constexpr Geometry kX4{480, 800, 100, 480};

class PxcPlanePackerTest : public ::testing::TestWithParam<Plane> {};

TEST_P(PxcPlanePackerTest, MatchesReferenceOnSmallGeometry) {
  const auto payload = randomPayload(kSmall, 42);
  EXPECT_EQ(packedPlane(GetParam(), kSmall, payload, 7), referencePlane(GetParam(), kSmall, payload));
}

TEST_P(PxcPlanePackerTest, MatchesReferenceOnX3Geometry) {
  const auto payload = randomPayload(kX3, 1234);
  EXPECT_EQ(packedPlane(GetParam(), kX3, payload, 80), referencePlane(GetParam(), kX3, payload));
}

TEST_P(PxcPlanePackerTest, MatchesReferenceOnX4Geometry) {
  const auto payload = randomPayload(kX4, 99);
  EXPECT_EQ(packedPlane(GetParam(), kX4, payload, 80), referencePlane(GetParam(), kX4, payload));
}

INSTANTIATE_TEST_SUITE_P(AllPlanes, PxcPlanePackerTest,
                         ::testing::Values(Plane::BwBase, Plane::BwFastDithered, Plane::Lsb, Plane::Msb));

TEST(PxcPlanePackerRules, SolidWhiteLeavesBwUntouchedAndPlanesEmpty) {
  Geometry g = kSmall;
  const int bytesPerRow = (g.logicalWidth + 3) / 4;
  std::vector<uint8_t> payload(static_cast<size_t>(bytesPerRow) * g.logicalHeight, 0xFF);  // all 3s
  EXPECT_EQ(packedPlane(Plane::BwBase, g, payload, 8),
            std::vector<uint8_t>(static_cast<size_t>(g.strideBytes) * g.physicalHeight, 0xFF));
  EXPECT_EQ(packedPlane(Plane::Lsb, g, payload, 8),
            std::vector<uint8_t>(static_cast<size_t>(g.strideBytes) * g.physicalHeight, 0x00));
  EXPECT_EQ(packedPlane(Plane::Msb, g, payload, 8),
            std::vector<uint8_t>(static_cast<size_t>(g.strideBytes) * g.physicalHeight, 0x00));
}

}  // namespace
