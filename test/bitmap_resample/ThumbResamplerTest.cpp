#include <BmpWriter.h>
#include <Print.h>
#include <ThumbResampler.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "activities/home/LibraryGridGeometry.h"

namespace {

class MemoryPrint : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t b) override {
    bytes.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    bytes.insert(bytes.end(), buf, buf + size);
    return size;
  }
};

class MemoryFile {
 public:
  explicit MemoryFile(const std::vector<uint8_t>& data) : data(data) {}
  bool seekSet(const size_t pos) {
    if (pos > data.size()) return false;
    cursor = pos;
    return true;
  }
  int read(void* out, const size_t n) {
    if (cursor >= data.size()) return 0;
    const size_t avail = std::min(n, data.size() - cursor);
    memcpy(out, data.data() + cursor, avail);
    cursor += avail;
    return static_cast<int>(avail);
  }

 private:
  const std::vector<uint8_t>& data;
  size_t cursor = 0;
};

std::vector<uint8_t> make2BitBmp(const int w, const int h, const uint8_t twoBitLevel) {
  MemoryPrint out;
  bmp_writer::writeHeader(out, w, h, 2, /*topDown=*/true);
  const int rowBytes = (w * 2 + 31) / 32 * 4;
  std::vector<uint8_t> row(static_cast<size_t>(rowBytes), 0);
  for (int x = 0; x < w; ++x) {
    row[x >> 2] |= static_cast<uint8_t>((twoBitLevel & 0x03) << (6 - ((x & 3) << 1)));
  }
  for (int y = 0; y < h; ++y) out.write(row.data(), row.size());
  return out.bytes;
}

TEST(ThumbResamplerTest, DownsamplesCoverBmpTo132x196TwoBitBmp) {
  const auto srcBmp = make2BitBmp(480, 720, 1);  // dark gray cover
  MemoryFile src(srcBmp);
  MemoryPrint dst;

  ASSERT_TRUE(thumb_resampler::downsample2BitBmp(src, dst, 132, 196));
  ASSERT_GE(dst.bytes.size(), 70u);
  EXPECT_EQ(dst.bytes[0], 'B');
  EXPECT_EQ(dst.bytes[1], 'M');
  const int32_t outW = static_cast<int32_t>(thumb_resampler::loadLE32(dst.bytes.data() + 18));
  const int32_t outH = -static_cast<int32_t>(thumb_resampler::loadLE32(dst.bytes.data() + 22));
  const uint16_t bpp = thumb_resampler::loadLE16(dst.bytes.data() + 28);
  EXPECT_EQ(bpp, 2u);
  EXPECT_LE(outW, 132);
  EXPECT_LE(outH, 196);
  EXPECT_GT(outW, 120);
  EXPECT_EQ(outH, 196);
  const size_t expectedRowBytes = static_cast<size_t>((outW * 2 + 31) / 32 * 4);
  EXPECT_EQ(dst.bytes.size(), 70u + expectedRowBytes * static_cast<size_t>(outH));
}

TEST(LibraryGridGeometryTest, Uses3x3OnX4ProAnd2x3OnC3) {
  const library_grid::Box portraitBody{16, 80, 448, 660};
  const auto x4pro = library_grid::computeLayout(portraitBody, /*highRamBoard=*/true);
  EXPECT_EQ(x4pro.cols, 3);
  EXPECT_EQ(x4pro.rows, 3);
  EXPECT_EQ(x4pro.perPage, 9);

  const auto c3 = library_grid::computeLayout(portraitBody, /*highRamBoard=*/false);
  EXPECT_EQ(c3.cols, 2);
  EXPECT_EQ(c3.rows, 3);
  EXPECT_EQ(c3.perPage, 6);

  for (int i = 0; i < x4pro.perPage; ++i) {
    EXPECT_GE(x4pro.tiles[i].cover.x, portraitBody.x);
    EXPECT_LE(x4pro.tiles[i].cover.x + x4pro.tiles[i].cover.width, portraitBody.x + portraitBody.width);
    EXPECT_LE(x4pro.tiles[i].progressBar.y + x4pro.tiles[i].progressBar.height, portraitBody.y + portraitBody.height);
  }
  EXPECT_EQ(library_grid::tileAt(x4pro, 9, x4pro.tiles[4].cell.x + 10, x4pro.tiles[4].cell.y + 10), 4);
  EXPECT_EQ(library_grid::movePage(2, 20, 9, 1), 11);
  EXPECT_EQ(library_grid::movePage(11, 20, 9, -1), 2);
}

}  // namespace
