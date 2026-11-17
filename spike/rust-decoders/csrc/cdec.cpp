#include "cdec.h"

#include <InflateReader.h>
#include <InflateStream.h>
#include <JPEGDEC.h>
#include <MinizConfig.h>
#include <PNGdec.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {

struct JpegCtx {
  rdec_row_cb cb;
  void* ctx;
  int width;
  uint8_t* strip;  // one MCU row, so rows leave top to bottom like the Rust side
};

int jpegDraw(JPEGDRAW* d) {
  auto* c = static_cast<JpegCtx*>(d->pUser);
  const auto* px = reinterpret_cast<const uint8_t*>(d->pPixels);
  const int used = d->x + d->iWidthUsed > c->width ? c->width - d->x : d->iWidthUsed;
  for (int r = 0; r < d->iHeight; r++) memcpy(c->strip + r * c->width + d->x, px + r * d->iWidth, used);
  if (d->x + d->iWidthUsed >= c->width) {
    for (int r = 0; r < d->iHeight; r++) c->cb(c->ctx, d->y + r, c->strip + r * c->width, c->width);
  }
  return 1;
}

constexpr int kMaxMcuRows = 16;

}  // namespace

int c_jpeg_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h) {
  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) return -3;
  if (!jpeg->openRAM(const_cast<uint8_t*>(data), static_cast<int>(len), jpegDraw)) return -1;
  JpegCtx c{cb, ctx, jpeg->getWidth(), nullptr};
  // Progressive files decode DC-only at 1/8 inside JPEGDEC; the output is then that size.
  if (jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE) c.width = (c.width + 7) / 8;
  std::unique_ptr<uint8_t[]> strip(new (std::nothrow) uint8_t[static_cast<size_t>(c.width) * kMaxMcuRows]);
  if (!strip) return -3;
  c.strip = strip.get();
  *w = c.width;
  *h = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE ? (jpeg->getHeight() + 7) / 8 : jpeg->getHeight();
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&c);
  const int rc = jpeg->decode(0, 0, 0);
  jpeg->close();
  return rc == 1 ? 0 : -2;
}

namespace {

// Copied from lib/Epub/Epub/converters/PngToFramebufferConverter.cpp (alphaLine == nullptr).
uint8_t readPackedSample(const uint8_t* pixels, int x, int bitsPerSample) {
  if (bitsPerSample == 8) return pixels[x];
  const int bitOffset = x * bitsPerSample;
  const int shift = 8 - bitsPerSample - (bitOffset & 7);
  const uint8_t mask = (1U << bitsPerSample) - 1;
  return (pixels[bitOffset >> 3] >> shift) & mask;
}

uint8_t expandSampleToByte(uint8_t sample, int bitsPerSample) {
  if (bitsPerSample == 8) return sample;
  const uint8_t maxSample = (1U << bitsPerSample) - 1;
  return static_cast<uint8_t>((sample * 255U) / maxSample);
}

void convertLineToGray(const uint8_t* pPixels, uint8_t* grayLine, int width, int pixelType, int bitsPerSample,
                       const uint8_t* palette, int hasAlpha, uint32_t transparentColor) {
  const auto store = [grayLine](const int x, const uint8_t gray, const uint8_t alpha) {
    grayLine[x] = static_cast<uint8_t>((gray * alpha + 255u * (255u - alpha)) / 255u);
  };
  switch (pixelType) {
    case PNG_PIXEL_GRAYSCALE:
      if (bitsPerSample == 8 && !hasAlpha) {
        memcpy(grayLine, pPixels, width);
      } else {
        for (int x = 0; x < width; x++) {
          const uint8_t sample = readPackedSample(pPixels, x, bitsPerSample);
          store(x, expandSampleToByte(sample, bitsPerSample),
                hasAlpha && sample == static_cast<uint8_t>(transparentColor) ? 0 : 255);
        }
      }
      break;
    case PNG_PIXEL_TRUECOLOR:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 3];
        const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        const uint32_t color = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        store(x, gray, hasAlpha && color == transparentColor ? 0 : 255);
      }
      break;
    case PNG_PIXEL_INDEXED:
      for (int x = 0; x < width; x++) {
        const uint8_t idx = readPackedSample(pPixels, x, bitsPerSample);
        const uint8_t* p = &palette[idx * 3];
        const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        store(x, gray, hasAlpha ? palette[768 + idx] : 255);
      }
      break;
    case PNG_PIXEL_GRAY_ALPHA:
      for (int x = 0; x < width; x++) store(x, pPixels[x * 2], pPixels[x * 2 + 1]);
      break;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 4];
        store(x, static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8), p[3]);
      }
      break;
    default:
      memset(grayLine, 128, width);
      break;
  }
}

struct PngCtx {
  rdec_row_cb cb;
  void* ctx;
  PNG* png;
  uint8_t* line;
};

int pngDraw(PNGDRAW* d) {
  auto* c = static_cast<PngCtx*>(d->pUser);
  convertLineToGray(d->pPixels, c->line, d->iWidth, d->iPixelType, d->iBpp, d->pPalette, d->iHasAlpha,
                    c->png->getTransparentColor());
  c->cb(c->ctx, d->y, c->line, d->iWidth);
  return 1;
}

}  // namespace

int c_png_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h) {
  std::unique_ptr<PNG> png(new (std::nothrow) PNG());
  if (!png) return -3;
  if (png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(len), pngDraw) != PNG_SUCCESS) return -1;
  // The firmware's own gates (PngToFramebufferConverter): 16-bit, interlaced and too-wide rows.
  const int bpp = png->getBpp(), type = png->getPixelType();
  if (png->isInterlaced() || (bpp != 8 && !((bpp == 1 || bpp == 2 || bpp == 4) &&
                                            (type == PNG_PIXEL_GRAYSCALE || type == PNG_PIXEL_INDEXED)))) {
    return -4;
  }
  std::unique_ptr<uint8_t[]> line(new (std::nothrow) uint8_t[png->getWidth()]);
  if (!line) return -3;
  *w = png->getWidth();
  *h = png->getHeight();
  PngCtx c{cb, ctx, png.get(), line.get()};
  const int rc = png->decode(&c, 0);
  png->close();
  return rc == PNG_SUCCESS ? 0 : -2;
}

namespace {
constexpr size_t kChunk = 4096;
}

int64_t c_tinfl_raw(const uint8_t* data, size_t len, uint32_t* sum) {
  InflateStream s;
  if (!s.init(true)) return -3;
  s.setSource(data, len);
  std::unique_ptr<uint8_t[]> out(new (std::nothrow) uint8_t[kChunk]);
  if (!out) return -3;
  int64_t total = 0;
  uint32_t acc = 0;
  for (;;) {
    size_t n = 0;
    const auto st = s.readAtMost(out.get(), kChunk, &n);
    for (size_t i = 0; i < n; i++) acc += out[i];
    total += n;
    if (st == InflateStream::Status::Done) break;
    if (st == InflateStream::Status::Error || n == 0) return -2;
  }
  *sum = acc;
  return total;
}

int64_t c_uzlib_raw(const uint8_t* data, size_t len, uint32_t* sum) {
  InflateReader r;
  if (!r.init(true)) return -3;
  r.setSource(data, len);
  std::unique_ptr<uint8_t[]> out(new (std::nothrow) uint8_t[kChunk]);
  if (!out) return -3;
  int64_t total = 0;
  uint32_t acc = 0;
  for (;;) {
    size_t n = 0;
    const bool ok = r.readAtMost(out.get(), kChunk, &n);
    for (size_t i = 0; i < n; i++) acc += out[i];
    total += n;
    if (!ok) return -2;
    if (n < kChunk) break;
  }
  *sum = acc;
  return total;
}
