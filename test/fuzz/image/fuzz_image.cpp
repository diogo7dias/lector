// libFuzzer harness for chapter and viewer image decode: ImageDimsProbe on the header, then
// ImageDecoderFactory's decoder as ImageBlock::render and BmpViewerActivity::renderPng call
// it, into an exact-size frame (or grayscale strip) buffer. Input layout, all-zero bytes
// being ImageBlock's case (exact size, dithered, cached, portrait X4, BW, whole frame):
//   byte 0   bit 0 fit-to-box (BmpViewer) instead of exact size, bit 1 no dither, bit 2 no
//            cache, bits 3-4 orientation, bits 5-6 render mode, bit 7 X3 panel
//   byte 1   bit 0 strip target, bit 1 keep alpha, bit 2 source crop, bits 3-7 probe chunk
//   bytes 2-5 x, y, width, height as fractions of the screen (ImageBlock's bounds check holds)
//   bytes 6-7 strip rows and top row, or crop fractions
//   bytes 8.. the image file; PNG when it starts 0x89, else JPEG
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"

namespace {

constexpr uint8_t kFill = 0x5A;

// Logical (x, y) of a physical pixel: the inverse of DirectPixelWriter::init's transform.
void toLogical(const GfxRenderer& r, const int phyX, const int phyY, int& x, int& y) {
  const int w = r.panelWidth, h = r.panelHeight;
  switch (r.orientation) {
    case GfxRenderer::Portrait:
      x = h - 1 - phyY, y = phyX;
      break;
    case GfxRenderer::LandscapeClockwise:
      x = w - 1 - phyX, y = h - 1 - phyY;
      break;
    case GfxRenderer::PortraitInverted:
      x = phyY, y = w - 1 - phyX;
      break;
    default:
      x = phyX, y = phyY;
      break;
  }
}

// Every bit that changed must belong to a pixel inside the image box.
void checkOnlyBoxWritten(const GfxRenderer& r, const uint8_t* buf, const int bx, const int by, const int bw,
                         const int bh) {
  const int widthBytes = r.getDisplayWidthBytes();
  for (int row = 0; row < r.rows; row++) {
    for (int col = 0; col < widthBytes; col++) {
      const uint8_t diff = buf[row * widthBytes + col] ^ kFill;
      for (int bit = 0; diff && bit < 8; bit++) {
        if (!(diff & (0x80 >> bit))) continue;
        int x, y;
        toLogical(r, col * 8 + bit, r.originY + row, x, y);
        if (x < bx || x >= bx + bw || y < by || y >= by + bh) {
          fprintf(stderr, "pixel written outside the image box: logical (%d,%d), box %d,%d %dx%d\n", x, y, bx, by, bw,
                  bh);
          abort();
        }
      }
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 9) return 0;
  const uint8_t mode = data[0], target = data[1];
  const std::string image(reinterpret_cast<const char*>(data + 8), size - 8);
  const bool png = static_cast<uint8_t>(image[0]) == 0x89;
  const std::string path = png ? "/.crosspoint/epub_1/img_0.png" : "/.crosspoint/epub_1/img_0.jpg";
  const std::string cachePath = "/.crosspoint/epub_1/img_0.pxc";

  Storage.reset();
  HalFile::readChunk = SIZE_MAX;
  Storage.put(path, image);

  // The chapter build probes the header as it streams out of the zip, in zip-sized chunks.
  ImageDimsProbe probe;
  const size_t chunk = 1 + (target >> 3) * 64;
  for (size_t off = 0; off < image.size(); off += chunk) {
    const size_t len = std::min(chunk, image.size() - off);
    if (probe.write(reinterpret_cast<const uint8_t*>(image.data() + off), len) != len) break;
  }
  ImageDimensions probed{};
  const bool probeOk = probe.getDimensions(probed);

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(path);
  if (!decoder) abort();
  ImageDimensions dims{};
  const bool dimsOk = decoder->getDimensions(path, dims);
  if (png && probeOk && dimsOk && (probed.width != dims.width || probed.height != dims.height)) {
    fprintf(stderr, "probe %dx%d != decoder %dx%d\n", probed.width, probed.height, dims.width, dims.height);
    abort();
  }

  GfxRenderer renderer;
  if (mode & 0x80) renderer.panelWidth = 792, renderer.panelHeight = 528;
  renderer.orientation = static_cast<GfxRenderer::Orientation>((mode >> 3) & 3);
  renderer.mode = static_cast<GfxRenderer::RenderMode>(((mode >> 5) & 3) % 3);
  const int sw = renderer.getScreenWidth(), sh = renderer.getScreenHeight();

  RenderConfig config;
  config.x = data[2] * (sw - 1) / 255;
  config.y = data[3] * (sh - 1) / 255;
  config.maxWidth = (sw - config.x) - data[4] * (sw - config.x - 1) / 255;
  config.maxHeight = (sh - config.y) - data[5] * (sh - config.y - 1) / 255;
  config.useExactDimensions = !(mode & 0x01);
  config.useDithering = !(mode & 0x02);
  if (!(mode & 0x04)) config.cachePath = cachePath;
  config.preserveAlpha = target & 0x02;
  if (target & 0x04) {
    config.sourceCropX = data[6] / 255.0f;
    config.sourceCropY = data[7] / 255.0f;
  }

  const int widthBytes = renderer.getDisplayWidthBytes();
  if (target & 0x01) {
    renderer.rows = 1 + data[6] % 96;
    renderer.originY = data[7] % (renderer.panelHeight - renderer.rows + 1);
  } else {
    renderer.rows = renderer.panelHeight;
  }
  const size_t bufSize = static_cast<size_t>(widthBytes) * renderer.rows;
  auto buf = std::make_unique<uint8_t[]>(bufSize);
  memset(buf.get(), kFill, bufSize);
  renderer.target = buf.get();

  const bool ok = decoder->decodeToFramebuffer(path, renderer, config);
  checkOnlyBoxWritten(renderer, buf.get(), config.x, config.y, config.maxWidth, config.maxHeight);

  // A finished cache must be what ImageBlock::renderFromCache reads back: a w,h header and
  // (w+3)/4 bytes per row, at the box size ImageBlock asked for.
  const auto it = Storage.files.find(cachePath);
  if (it != Storage.files.end()) {
    const std::string& pxc = *it->second;
    uint16_t w = 0, h = 0;
    if (!ok || pxc.size() < 4) abort();
    memcpy(&w, pxc.data(), 2);
    memcpy(&h, pxc.data() + 2, 2);
    if (pxc.size() != 4 + static_cast<size_t>((w + 3) / 4) * h) abort();
    if (config.useExactDimensions && (w != config.maxWidth || h != config.maxHeight)) abort();
  }

  if (getenv("FUZZ_TRACE"))
    fprintf(stderr, "%s probe %d %dx%d, dims %d %dx%d, decode %d, cache %s\n", png ? "png" : "jpg", probeOk,
            probed.width, probed.height, dimsOk, dims.width, dims.height, ok,
            it != Storage.files.end() ? "written" : "none");
  return 0;
}
