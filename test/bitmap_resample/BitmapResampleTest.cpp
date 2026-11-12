#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "Bitmap.h"

namespace {

void put16(std::vector<uint8_t>& v, const size_t at, const uint16_t x) {
  v[at] = x & 0xFF;
  v[at + 1] = x >> 8;
}
void put32(std::vector<uint8_t>& v, const size_t at, const uint32_t x) {
  for (int i = 0; i < 4; i++) v[at + i] = (x >> (8 * i)) & 0xFF;
}

// A 24-bit top-down BMP whose gray at (x, y) is gray(x, y).
std::vector<uint8_t> makeBmp24(const int w, const int h, const std::function<uint8_t(int, int)>& gray) {
  const int rowBytes = (w * 3 + 3) / 4 * 4;
  std::vector<uint8_t> v(54 + rowBytes * h, 0);
  v[0] = 'B';
  v[1] = 'M';
  put32(v, 2, v.size());
  put32(v, 10, 54);
  put32(v, 14, 40);
  put32(v, 18, w);
  put32(v, 22, static_cast<uint32_t>(-h));
  put16(v, 26, 1);
  put16(v, 28, 24);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      for (int c = 0; c < 3; c++) v[54 + y * rowBytes + x * 3 + c] = gray(x, y);
  return v;
}

std::vector<std::vector<uint8_t>> readAll(const Bitmap& bmp) {
  std::vector<uint8_t> row(bmp.getRowBytes());
  std::vector<std::vector<uint8_t>> out;
  for (int y = 0; y < bmp.getHeight(); y++) {
    std::vector<uint8_t> packed((bmp.getWidth() + 3) / 4);
    EXPECT_EQ(bmp.readNextRow(packed.data(), row.data()), BmpReaderError::Ok) << "row " << y;
    out.push_back(packed);
  }
  return out;
}

uint8_t level(const std::vector<uint8_t>& packed, const int x) { return (packed[x / 4] >> (6 - (x % 4) * 2)) & 3; }

}  // namespace

TEST(BitmapResample, FitsInsideBoundsAndKeepsAspect) {
  HalFile file(makeBmp24(1600, 960, [](int, int) { return 128; }));
  Bitmap bmp(file, true);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  ASSERT_TRUE(bmp.ditherToFit(800, 800));
  EXPECT_EQ(bmp.getWidth(), 800);
  EXPECT_EQ(bmp.getHeight(), 480);
  EXPECT_EQ(readAll(bmp).size(), 480u);
  std::vector<uint8_t> extra(1), row(bmp.getRowBytes());
  EXPECT_NE(bmp.readNextRow(extra.data(), row.data()), BmpReaderError::Ok);
}

TEST(BitmapResample, NoOpWhenItFitsOrWithoutDithering) {
  HalFile small(makeBmp24(100, 100, [](int, int) { return 128; }));
  Bitmap fits(small, true);
  ASSERT_EQ(fits.parseHeaders(), BmpReaderError::Ok);
  EXPECT_FALSE(fits.ditherToFit(800, 480));
  EXPECT_EQ(fits.getWidth(), 100);

  HalFile big(makeBmp24(1600, 960, [](int, int) { return 128; }));
  Bitmap plain(big);
  ASSERT_EQ(plain.parseHeaders(), BmpReaderError::Ok);
  EXPECT_FALSE(plain.ditherToFit(800, 480));
  EXPECT_EQ(plain.getWidth(), 1600);
}

// The defect this fixes: a 2 px dark line at x 200-201 in a 4:1 reduction falls between
// the pixels nearest-neighbour kept (x 202). Averaged, it is mid gray on every row.
TEST(BitmapResample, ThinLineSurvivesReduction) {
  HalFile file(makeBmp24(400, 400, [](int x, int) { return (x == 200 || x == 201) ? 0 : 255; }));
  Bitmap bmp(file, true);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  ASSERT_TRUE(bmp.ditherToFit(100, 100));
  const auto rows = readAll(bmp);
  int dark = 0;
  for (const auto& r : rows) {
    for (int x = 0; x < 100; x++) {
      if (x != 50) {
        EXPECT_EQ(level(r, x), 3) << "x " << x;
      }
    }
    if (level(r, 50) < 3) dark++;
  }
  // 128 gray dithers to a mix of dark dots and white; nearest-neighbour drew none.
  EXPECT_GT(dark, 0);
}

TEST(BitmapResample, RewindRepeatsTheSameRows) {
  HalFile file(makeBmp24(333, 517, [](int x, int y) { return static_cast<uint8_t>((x * 7 + y * 3) & 0xFF); }));
  Bitmap bmp(file, true);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  ASSERT_TRUE(bmp.ditherToFit(120, 200));
  const auto first = readAll(bmp);
  ASSERT_EQ(bmp.rewindToData(), BmpReaderError::Ok);
  EXPECT_EQ(readAll(bmp), first);
}

TEST(BitmapResample, FlatGrayStaysFlatOnAverage) {
  HalFile file(makeBmp24(1000, 1000, [](int, int) { return 255; }));
  Bitmap bmp(file, true);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  ASSERT_TRUE(bmp.ditherToFit(300, 300));
  for (const auto& r : readAll(bmp)) {
    for (int x = 0; x < bmp.getWidth(); x++) ASSERT_EQ(level(r, x), 3);
  }
}
