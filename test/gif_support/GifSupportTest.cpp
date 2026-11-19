#include <GfxRenderer.h>
#include <GifCommon.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Epub/converters/GifToFramebufferConverter.h"
#include "Epub/converters/ImageDimsProbe.h"

namespace {

struct GifSpec {
  GifSpec(const int w, const int h) : canvasW(w), canvasH(h) {}
  int canvasW, canvasH;
  int frameX = 0, frameY = 0, frameW = -1, frameH = -1;  // -1: the canvas
  std::vector<uint8_t> pixels;                           // palette indices, frame order
  bool interlaced = false;
  int transparent = -1;
};

void put16(std::string& s, int v) {
  s.push_back(static_cast<char>(v & 0xFF));
  s.push_back(static_cast<char>(v >> 8));
}

// A GIF with a 4-gray global palette (0, 85, 170, 255) and minimum code size 2. The LZW
// stream is literals only, with a clear code every two pixels so the 3-bit code width never
// has to grow.
std::string makeGif(const GifSpec& g) {
  const int fw = g.frameW < 0 ? g.canvasW : g.frameW, fh = g.frameH < 0 ? g.canvasH : g.frameH;
  std::string s = "GIF89a";
  put16(s, g.canvasW);
  put16(s, g.canvasH);
  s.push_back(static_cast<char>(0xF1));  // global table, 4 entries
  s.push_back(0);
  s.push_back(0);
  for (int i = 0; i < 4; i++) s.append(3, static_cast<char>(i * 85));
  if (g.transparent >= 0) {
    s.append("\x21\xF9\x04\x01", 4);
    put16(s, 0);
    s.push_back(static_cast<char>(g.transparent));
    s.push_back(0);
  }
  s.push_back(0x2C);
  put16(s, g.frameX);
  put16(s, g.frameY);
  put16(s, fw);
  put16(s, fh);
  s.push_back(g.interlaced ? 0x40 : 0);
  s.push_back(2);  // minimum code size

  uint32_t acc = 0;
  int bits = 0;
  std::string lzw;
  const auto emit = [&](const int code) {
    acc |= static_cast<uint32_t>(code) << bits;
    for (bits += 3; bits >= 8; bits -= 8) {
      lzw.push_back(static_cast<char>(acc & 0xFF));
      acc >>= 8;
    }
  };
  for (size_t i = 0; i < g.pixels.size(); i++) {
    if (i % 2 == 0) emit(4);  // clear
    emit(g.pixels[i]);
  }
  emit(5);  // end of information
  if (bits > 0) lzw.push_back(static_cast<char>(acc & 0xFF));
  for (size_t off = 0; off < lzw.size(); off += 255) {
    const size_t n = std::min<size_t>(255, lzw.size() - off);
    s.push_back(static_cast<char>(n));
    s.append(lzw, off, n);
  }
  s.push_back(0);
  s.push_back(0x3B);
  return s;
}

// A flat frame of one palette index.
GifSpec flat(const int w, const int h, const int index) {
  GifSpec g{w, h};
  g.pixels.assign(static_cast<size_t>(w) * h, static_cast<uint8_t>(index));
  return g;
}

struct Decoded {
  bool ok;
  std::string pxc;  // the .pxc: w, h (2 bytes each), then (w + 3) / 4 bytes a row
};

Decoded decode(const std::string& gif, const int boxW, const int boxH, const bool exact = true) {
  Storage.reset();
  Storage.put("/book/img.gif", gif);
  GfxRenderer renderer;
  auto frame = std::make_unique<uint8_t[]>(static_cast<size_t>(renderer.panelWidth / 8) * renderer.panelHeight);
  renderer.rows = renderer.panelHeight;
  renderer.target = frame.get();
  RenderConfig config;
  config.x = 0;
  config.y = 0;
  config.maxWidth = boxW;
  config.maxHeight = boxH;
  config.useDithering = false;
  config.useExactDimensions = exact;
  config.cachePath = "/book/img.t2.pxc";
  GifToFramebufferConverter decoder;
  Decoded d{decoder.decodeToFramebuffer("/book/img.gif", renderer, config), {}};
  const auto it = Storage.files.find("/book/img.t2.pxc");
  if (it != Storage.files.end()) d.pxc = *it->second;
  return d;
}

// Level (0 black .. 3 white) of pixel (x, y) in a cache.
int level(const std::string& pxc, const int x, const int y) {
  uint16_t w;
  memcpy(&w, pxc.data(), 2);
  const uint8_t byte = static_cast<uint8_t>(pxc[4 + y * ((w + 3) / 4) + x / 4]);
  return (byte >> (6 - 2 * (x % 4))) & 3;
}

bool probe(const std::string& bytes, ImageDimensions& dims, const size_t chunk = 1) {
  ImageDimsProbe p;
  for (size_t off = 0; off < bytes.size(); off += chunk) {
    const size_t n = std::min(chunk, bytes.size() - off);
    if (p.write(reinterpret_cast<const uint8_t*>(bytes.data() + off), n) != n) break;
  }
  return p.getDimensions(dims);
}

}  // namespace

TEST(GifProbe, ReadsLogicalScreenSizeFromEitherVersion) {
  std::string gif = makeGif(flat(300, 200, 0));
  ImageDimensions d{};
  EXPECT_TRUE(probe(gif, d));
  EXPECT_EQ(300, d.width);
  EXPECT_EQ(200, d.height);
  gif[4] = '7';
  gif[5] = 'a';
  EXPECT_TRUE(probe(gif, d, 4096));
  EXPECT_EQ(300, d.width);
}

TEST(GifProbe, RejectsBadSignatureAndEmptyCanvas) {
  ImageDimensions d{};
  std::string gif = makeGif(flat(8, 8, 0));
  gif[4] = '8';
  EXPECT_FALSE(probe(gif, d));
  gif = makeGif(flat(8, 8, 0));
  gif[5] = 'b';
  EXPECT_FALSE(probe(gif, d));
  EXPECT_FALSE(probe("GIF89a\x08", d));
  EXPECT_FALSE(probe(makeGif(GifSpec{0, 8}), d));
}

TEST(GifHeader, FindsFrameAndInterlaceFlag) {
  GifSpec spec = flat(8, 8, 1);
  spec.frameX = 2;
  spec.frameY = 3;
  spec.frameW = 4;
  spec.frameH = 4;
  spec.pixels.resize(16);
  spec.transparent = 0;
  spec.interlaced = true;
  Storage.reset();
  Storage.put("/g", makeGif(spec));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("T", "/g", file));
  GifBasicInfo info;
  ASSERT_TRUE(GifCommon::readBasicInfo(file, info));
  EXPECT_EQ(8, info.canvasWidth);
  EXPECT_EQ(2, info.frameX);
  EXPECT_EQ(3, info.frameY);
  EXPECT_EQ(4, info.frameWidth);
  EXPECT_TRUE(info.interlaced);
  EXPECT_EQ(0u, file.position());
}

TEST(GifHeader, StopsOnTruncationTrailerAndExtensionFlood) {
  GifBasicInfo info;
  const std::string good = makeGif(flat(8, 8, 0));
  for (const size_t cut : {0u, 5u, 12u, 13u, 20u, 28u}) {
    Storage.reset();
    Storage.put("/g", good.substr(0, cut));
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("T", "/g", file));
    EXPECT_FALSE(GifCommon::readBasicInfo(file, info)) << "cut at " << cut;
  }
  // Header, 4-entry table, then nothing but empty extension blocks.
  std::string flood = good.substr(0, 13 + 12);
  for (int i = 0; i < 100000; i++) flood.append("\x21\xFE\x00", 3);
  Storage.put("/g", flood);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("T", "/g", file));
  EXPECT_FALSE(GifCommon::readBasicInfo(file, info));
}

TEST(GifLimitedWriter, ShortWriteAtTheLimit) {
  HalFile file;
  Storage.reset();
  ASSERT_TRUE(Storage.openFileForWrite("T", "/out", file));
  GifLimitedWriter limited(file, 10);
  const uint8_t bytes[8] = {};
  EXPECT_EQ(8u, limited.write(bytes, 8));
  EXPECT_EQ(0u, limited.write(bytes, 8));
  EXPECT_EQ(2u, limited.write(bytes, 2));
}

TEST(GifDecode, WritesBlackAndWhiteHalvesToTheCache) {
  GifSpec g{8, 8};
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) g.pixels.push_back(x < 4 ? 0 : 3);
  const Decoded d = decode(makeGif(g), 8, 8);
  ASSERT_TRUE(d.ok);
  ASSERT_EQ(4u + 2 * 8, d.pxc.size());
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) EXPECT_EQ(x < 4 ? 0 : 3, level(d.pxc, x, y)) << x << "," << y;
}

TEST(GifDecode, TransparentPixelsAndUncoveredRowsAreWhite) {
  // 8x8 canvas, a 4x4 frame at (2,2): index 0 is transparent, index 1 is dark.
  GifSpec g{8, 8};
  g.frameX = g.frameY = 2;
  g.frameW = g.frameH = 4;
  g.transparent = 0;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) g.pixels.push_back(x == 1 ? 0 : 1);
  const Decoded d = decode(makeGif(g), 8, 8);
  ASSERT_TRUE(d.ok);
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      const bool inFrame = x >= 2 && x < 6 && y >= 2 && y < 6;
      const int want = inFrame && x != 3 ? level(d.pxc, 2, 2) : 3;
      EXPECT_EQ(want, level(d.pxc, x, y)) << x << "," << y;
    }
  }
  EXPECT_LT(level(d.pxc, 2, 2), 3);  // the dark index is not white
}

TEST(GifDecode, ScalesToTheBoxAndKeepsAspectWhenNotExact) {
  const Decoded exact = decode(makeGif(flat(16, 16, 0)), 4, 4);
  ASSERT_TRUE(exact.ok);
  uint16_t w, h;
  memcpy(&w, exact.pxc.data(), 2);
  memcpy(&h, exact.pxc.data() + 2, 2);
  EXPECT_EQ(4, w);
  EXPECT_EQ(4, h);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) EXPECT_EQ(0, level(exact.pxc, x, y));

  const Decoded fit = decode(makeGif(flat(16, 8, 0)), 40, 4, /*exact=*/false);  // 16x8 into 40x4: scale 0.5
  ASSERT_TRUE(fit.ok);
  memcpy(&w, fit.pxc.data(), 2);
  memcpy(&h, fit.pxc.data() + 2, 2);
  EXPECT_EQ(8, w);
  EXPECT_EQ(4, h);
}

TEST(GifDecode, UpscaleLeavesNoBlackGaps) {
  const Decoded d = decode(makeGif(flat(2, 2, 3)), 7, 5);
  ASSERT_TRUE(d.ok);
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 7; x++) EXPECT_EQ(3, level(d.pxc, x, y)) << x << "," << y;
}

TEST(GifDecode, RefusesWhatItCannotDrawWithoutLeavingACache) {
  GifSpec interlaced = flat(8, 8, 0);
  interlaced.interlaced = true;
  Decoded d = decode(makeGif(interlaced), 8, 8);
  EXPECT_FALSE(d.ok);
  EXPECT_TRUE(d.pxc.empty());

  std::string wide = makeGif(flat(8, 8, 0));
  wide[6] = static_cast<char>(0x01);  // canvas 3073 wide: past MAX_WIDTH, the frame says 8
  wide[7] = static_cast<char>(0x0C);
  d = decode(wide, 8, 8);
  EXPECT_FALSE(d.ok);
  EXPECT_TRUE(d.pxc.empty());

  EXPECT_FALSE(decode("GIF89a", 8, 8).ok);
}
