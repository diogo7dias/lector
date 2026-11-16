#include <JPEGDEC.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// edges.jpg is a 45x30 gray baseline JPEG at quality 95 (quantizers 1-2) with hard black and
// white edges, saved by Pillow; edges.pgm is libjpeg's decode of it (Pillow 12.2.0).
namespace {

struct Gray {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
};

std::vector<uint8_t> readFile(const std::string& name) {
  std::ifstream in(std::string(FIXTURE_DIR) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Gray readPgm(const std::string& name) {
  const std::vector<uint8_t> bytes = readFile(name);
  Gray g;
  int maxVal = 0, headerLen = 0;
  if (sscanf(reinterpret_cast<const char*>(bytes.data()), "P5 %d %d %d%n", &g.width, &g.height, &maxVal, &headerLen) !=
      3) {
    return {};
  }
  g.pixels.assign(bytes.begin() + headerLen + 1, bytes.end());
  return g;
}

int copyBlock(JPEGDRAW* d) {
  auto* out = static_cast<Gray*>(d->pUser);
  const auto* px = reinterpret_cast<const uint8_t*>(d->pPixels);
  for (int r = 0; r < d->iHeight && d->y + r < out->height; r++) {
    for (int c = 0; c < d->iWidthUsed && d->x + c < out->width; c++) {
      out->pixels[(d->y + r) * out->width + d->x + c] = px[r * d->iWidth + c];
    }
  }
  return 1;
}

Gray decode(std::vector<uint8_t>& jpegBytes, const int scaleOption, const int denom) {
  auto jpeg = std::make_unique<JPEGDEC>();
  Gray g;
  if (!jpeg->openRAM(jpegBytes.data(), static_cast<int>(jpegBytes.size()), copyBlock)) return g;
  g.width = (jpeg->getWidth() + denom - 1) / denom;
  g.height = (jpeg->getHeight() + denom - 1) / denom;
  g.pixels.assign(static_cast<size_t>(g.width) * g.height, 0);
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&g);
  if (!jpeg->decode(0, 0, scaleOption)) g.pixels.clear();
  jpeg->close();
  return g;
}

// libjpeg's pixels averaged over denom x denom cells, edge pixels repeated past the border.
double cellMean(const Gray& ref, const int cx, const int cy, const int denom) {
  double sum = 0;
  for (int y = 0; y < denom; y++) {
    for (int x = 0; x < denom; x++) {
      const int sx = std::min(cx * denom + x, ref.width - 1);
      const int sy = std::min(cy * denom + y, ref.height - 1);
      sum += ref.pixels[sy * ref.width + sx];
    }
  }
  return sum / (denom * denom);
}

}  // namespace

TEST(JpegDecodeAccuracy, FullScaleMatchesLibjpeg) {
  std::vector<uint8_t> jpegBytes = readFile("edges.jpg");
  const Gray ref = readPgm("edges.pgm");
  const Gray got = decode(jpegBytes, 0, 1);
  ASSERT_EQ(ref.pixels.size(), 45u * 30u);
  ASSERT_EQ(got.pixels.size(), ref.pixels.size());
  int worst = 0;
  for (size_t i = 0; i < ref.pixels.size(); i++) worst = std::max(worst, std::abs(got.pixels[i] - ref.pixels[i]));
  EXPECT_LE(worst, 3) << "levels off libjpeg";
}

// The 1/4 decode keeps only F(0,0), F(0,1), F(1,0) and F(1,1), so it can only approach the 4x4
// cell means: the bound is on the mean error.
TEST(JpegDecodeAccuracy, QuarterScaleApproachesCellMeans) {
  std::vector<uint8_t> jpegBytes = readFile("edges.jpg");
  const Gray ref = readPgm("edges.pgm");
  const Gray got = decode(jpegBytes, JPEG_SCALE_QUARTER, 4);
  ASSERT_EQ(got.pixels.size(), 12u * 8u);
  double total = 0;
  for (int y = 0; y < got.height; y++) {
    for (int x = 0; x < got.width; x++) total += std::fabs(got.pixels[y * got.width + x] - cellMean(ref, x, y, 4));
  }
  EXPECT_LE(total / got.pixels.size(), 8.5) << "mean levels off the cell means";
}
