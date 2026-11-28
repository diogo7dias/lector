#include "GifToFramebufferConverter.h"

#include <AnimatedGIF.h>
#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GifCommon.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context for AnimatedGIF's draw callback (pDraw->pUser). The file callbacks get the
// HalFile through pFile->fHandle.
struct GifContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};
  int srcWidth{0};
  int srcHeight{0};
  int dstWidth{0};
  int dstHeight{0};
  int lastDstY{-1};
  int nextExpectedSrcY{0};
  const uint8_t* activePalette24{nullptr};
  uint8_t grayPalette[256] = {0};
  uint8_t* grayRow{nullptr};  // one canvas row, 8-bit gray
  PixelCache cache;
  bool caching{false};
  bool success{true};
  uint32_t lastYieldMs{0};
};

void updateGrayPalette(const GIFDRAW* pDraw, GifContext& ctx) {
  const uint8_t* palette24 = pDraw->pPalette24;
  if (palette24 == nullptr || palette24 == ctx.activePalette24) return;

  ctx.activePalette24 = palette24;
  for (int i = 0; i < 256; ++i) {
    const uint8_t* pixel = &palette24[i * 3];
    ctx.grayPalette[i] = static_cast<uint8_t>((pixel[0] * 77 + pixel[1] * 150 + pixel[2] * 29) >> 8);
  }
}

// Emit the output rows source row srcY maps to. Downscaling folds several source rows onto
// one output row; upscaling repeats a source row across its whole output range (a gap would
// stay black in the streamed cache).
void renderCanvasRow(const uint8_t* grayRow, const int srcY, GifContext& ctx) {
  int firstDstY = (srcY * ctx.dstHeight) / ctx.srcHeight;
  int endDstY = firstDstY + 1;
  if (ctx.dstHeight > ctx.srcHeight) {
    endDstY = ((srcY + 1) * ctx.dstHeight) / ctx.srcHeight;
  }

  if (firstDstY <= ctx.lastDstY) firstDstY = ctx.lastDstY + 1;
  if (firstDstY >= endDstY || firstDstY >= ctx.dstHeight) return;
  if (endDstY > ctx.dstHeight) endDstY = ctx.dstHeight;

  DirectPixelWriter pw;
  pw.init(*ctx.renderer);

  for (int dstY = firstDstY; dstY < endDstY; ++dstY) {
    ctx.lastDstY = dstY;
    const int outY = ctx.config->y + dstY;
    if (outY < 0 || outY >= ctx.screenHeight) continue;

    pw.beginRow(outY);

    // A flush failure stops caching for the rest of the decode (finalize drops the file),
    // the picture still draws.
    DirectCacheWriter cw;
    bool caching = ctx.caching;
    if (caching) {
      if (!ctx.cache.advanceTo(dstY)) {
        caching = false;
        ctx.caching = false;
      } else {
        cw.init(ctx.cache.buffer, ctx.cache.bytesPerRow, ctx.cache.bandRows, ctx.cache.originX);
        cw.beginRow(outY, ctx.config->y + ctx.cache.bandStart);
      }
    }

    int srcX = 0;
    int error = 0;
    for (int dstX = 0; dstX < ctx.dstWidth; ++dstX) {
      const int outX = ctx.config->x + dstX;
      if (outX >= 0 && outX < ctx.screenWidth) {
        const uint8_t level = quantize4Level(grayRow[srcX], outX, outY, ctx.config->useDithering);
        pw.writePixel(outX, level);
        if (caching) cw.writePixel(outX, level);
      }

      // Bresenham-style stepping: srcX advances srcWidth/dstWidth per output pixel.
      error += ctx.srcWidth;
      while (error >= ctx.dstWidth) {
        error -= ctx.dstWidth;
        if (srcX < ctx.srcWidth - 1) srcX++;
      }
    }
  }
}

// Canvas rows the frame does not cover (a frame smaller than the canvas, or an offset one)
// are white.
void renderGapRowsUpTo(const int canvasY, GifContext& ctx) {
  if (ctx.nextExpectedSrcY >= canvasY) return;
  memset(ctx.grayRow, 255, static_cast<size_t>(ctx.srcWidth));
  while (ctx.nextExpectedSrcY < canvasY && ctx.nextExpectedSrcY < ctx.srcHeight) {
    renderCanvasRow(ctx.grayRow, ctx.nextExpectedSrcY, ctx);
    ctx.nextExpectedSrcY++;
  }
}

void* gifOpen(const char* filename, int32_t* size) {
  return ImageToFramebufferDecoder::openDecoderFile("GIF", filename, size);
}

int32_t gifRead(GIFFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) return 0;
  const int32_t bytesRead = file->read(pBuf, len);
  if (bytesRead < 0) {
    LOG_ERR("GIF", "Read failed at offset %d for %d bytes", pFile->iPos, len);
    return 0;
  }
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t gifSeek(GIFFILE* pFile, int32_t position) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) return -1;
  if (position < 0) position = 0;
  if (position >= pFile->iSize) position = pFile->iSize - 1;
  if (!file->seek(position)) {
    LOG_ERR("GIF", "Seek failed at offset %d", position);
    return -1;
  }
  pFile->iPos = position;
  return position;
}

void gifDrawCallback(GIFDRAW* pDraw) {
  auto* ctx = reinterpret_cast<GifContext*>(pDraw->pUser);
  if (!ctx || !ctx->success || !ctx->grayRow) return;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  const int canvasY = pDraw->iY + pDraw->y;
  if (canvasY < ctx->nextExpectedSrcY || canvasY >= ctx->srcHeight) {
    // A row out of order or off the canvas: the streaming output only walks down.
    ctx->success = false;
    return;
  }

  renderGapRowsUpTo(canvasY, *ctx);
  memset(ctx->grayRow, 255, static_cast<size_t>(ctx->srcWidth));
  updateGrayPalette(pDraw, *ctx);

  const uint8_t* pixels = pDraw->pPixels;
  const int startX = pDraw->iX;
  const int endX = startX + pDraw->iWidth;
  const bool hasTransparency = pDraw->ucHasTransparency != 0;
  const uint8_t transparent = pDraw->ucTransparent;

  for (int srcX = startX, i = 0; srcX < endX && srcX < ctx->srcWidth; ++srcX, ++i) {
    if (srcX < 0) continue;
    const uint8_t index = pixels[i];
    if (hasTransparency && index == transparent) continue;
    ctx->grayRow[srcX] = ctx->grayPalette[index];
  }

  renderCanvasRow(ctx->grayRow, canvasY, *ctx);
  ctx->nextExpectedSrcY = canvasY + 1;
}

// The decoder is one ~27 KB block (GIFIMAGE: 4 KB file buffer, 8 KB code table, 8 KB pixel
// stack, MAX_WIDTH line, two palettes); it lives only while a GIF decodes. Headroom matches PNG.
constexpr size_t GIF_DECODER_SIZE = sizeof(AnimatedGIF);
constexpr size_t MIN_FREE_HEAP_FOR_GIF = GIF_DECODER_SIZE + 16 * 1024;

bool readGifInfo(const std::string& imagePath, GifBasicInfo& info) {
  HalFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    LOG_ERR("GIF", "Failed to open source for header inspection: %s", imagePath.c_str());
    return false;
  }
  return GifCommon::readBasicInfo(file, info);
}

}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  GifBasicInfo info;
  if (!readGifInfo(imagePath, info)) return false;
  return validateAndStoreDimensions(info.canvasWidth, info.canvasHeight, out, "GIF");
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  GifBasicInfo info;
  if (!readGifInfo(imagePath, info)) return false;
  if (info.interlaced) {
    // Interlaced rows arrive out of order; the streaming output cannot place them.
    warnUnsupportedFeature("interlaced GIF", imagePath);
    return false;
  }
  if (info.canvasWidth > MAX_WIDTH) {
    LOG_ERR("GIF", "GIF canvas too wide for AnimatedGIF: %u px (max %d): %s", info.canvasWidth, MAX_WIDTH,
            imagePath.c_str());
    return false;
  }
  ImageDimensions source;
  if (!validateAndStoreDimensions(info.canvasWidth, info.canvasHeight, source, "GIF")) return false;

  const size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_GIF));
    return false;
  }

  auto gif = makeUniqueNoThrow<AnimatedGIF>();
  if (!gif) {
    LOG_ERR("GIF", "OOM: AnimatedGIF decoder (%u bytes)", static_cast<unsigned>(GIF_DECODER_SIZE));
    return false;
  }
  auto ctxPtr = makeUniqueNoThrow<GifContext>();
  if (!ctxPtr) {
    LOG_ERR("GIF", "OOM: GIF context");
    return false;
  }
  GifContext& ctx = *ctxPtr;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();
  ctx.srcWidth = source.width;
  ctx.srcHeight = source.height;

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    ctx.dstWidth = config.maxWidth;
    ctx.dstHeight = config.maxHeight;
  } else {
    const float scaleX = static_cast<float>(config.maxWidth) / ctx.srcWidth;
    const float scaleY = static_cast<float>(config.maxHeight) / ctx.srcHeight;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale > 1.0f) scale = 1.0f;  // never upscale
    ctx.dstWidth = static_cast<int>(ctx.srcWidth * scale);
    ctx.dstHeight = static_cast<int>(ctx.srcHeight * scale);
  }
  if (ctx.dstWidth <= 0 || ctx.dstHeight <= 0) {
    LOG_ERR("GIF", "Degenerate output size %dx%d: %s", ctx.dstWidth, ctx.dstHeight, imagePath.c_str());
    return false;
  }

  auto grayRow = makeUniqueNoThrow<uint8_t[]>(ctx.srcWidth);
  if (!grayRow) {
    LOG_ERR("GIF", "OOM: GIF row buffer (%d bytes)", ctx.srcWidth);
    return false;
  }
  ctx.grayRow = grayRow.get();

  gif->begin(GIF_PALETTE_RGB888);
  gif->setDrawType(GIF_DRAW_RAW);
  const int rcOpen = gif->open(imagePath.c_str(), gifOpen, ImageToFramebufferDecoder::closeDecoderFile, gifRead,
                               gifSeek, gifDrawCallback);
  const ScopedCleanup cleanup{[&gif]() { gif->close(); }};
  if (rcOpen != 1) {
    LOG_ERR("GIF", "AnimatedGIF open failed (err %d): %s", gif->getLastError(), imagePath.c_str());
    return false;
  }

  // One output row per source row at most, so a one-row band is enough (see PNG).
  ctx.caching = !config.cachePath.empty();
  if (ctx.caching && !ctx.cache.begin(config.cachePath, ctx.dstWidth, ctx.dstHeight, config.x, config.y, 1)) {
    LOG_ERR("GIF", "Failed to start cache stream, continuing without caching");
    ctx.caching = false;
  }

  int delayMs = 0;
  ctx.lastYieldMs = millis();
  const int frameState = gif->playFrame(false, &delayMs, &ctx);
  const int err = gif->getLastError();
  if (!ctx.success || (err != GIF_SUCCESS && err != GIF_EMPTY_FRAME)) {
    LOG_ERR("GIF", "GIF decode failed (state %d, err %d): %s", frameState, err, imagePath.c_str());
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  renderGapRowsUpTo(ctx.srcHeight, ctx);
  if (!ctx.success) {
    if (ctx.caching) ctx.cache.abort();
    return false;
  }
  if (frameState > 0) LOG_DBG("GIF", "Animated GIF, first frame only: %s", imagePath.c_str());

  if (ctx.caching) ctx.cache.finalize();
  return true;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}
