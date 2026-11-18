#include "GifToBmpConverter.h"

#include <AnimatedGIF.h>
#include <Arduino.h>
#include <BmpWriter.h>
#include <GifCommon.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "BitmapHelpers.h"

namespace {

// Same limits as the PNG cover path: the source canvas, and so the work, is bounded.
constexpr int MAX_CANVAS_HEIGHT = 3072;

// The decoder is one ~27 KB block (see GifToFramebufferConverter) next to the row buffers
// and the ditherer; the margin matches the framebuffer path's.
constexpr size_t MIN_FREE_HEAP = sizeof(AnimatedGIF) + 32 * 1024;

struct GifBmpContext {
  Print* bmpOut{nullptr};
  int srcWidth{0};
  int srcHeight{0};
  int outWidth{0};
  int outHeight{0};
  int bytesPerRow{0};
  bool needsScaling{false};
  uint32_t scaleX_fp{1u << 16};
  uint32_t scaleY_fp{1u << 16};
  int currentOutY{0};
  uint32_t nextOutYSrcStart{0};
  int nextExpectedSrcY{0};
  const uint8_t* activePalette24{nullptr};
  uint8_t grayPalette[256] = {0};
  uint8_t* bmpRow{nullptr};   // bytesPerRow
  uint8_t* grayRow{nullptr};  // srcWidth
  std::unique_ptr<uint32_t[]> rowAccum;
  std::unique_ptr<uint16_t[]> rowCount;
  std::unique_ptr<AtkinsonDitherer> ditherer;
  bool success{true};
  uint8_t rowsSinceYield{0};
};

void writeBmpRow(GifBmpContext& ctx) {
  if (ctx.bmpOut->write(ctx.bmpRow, ctx.bytesPerRow) != static_cast<size_t>(ctx.bytesPerRow)) {
    LOG_ERR("GIF", "Short BMP row write");
    ctx.success = false;
  }
  if (++ctx.rowsSinceYield >= 8) {
    ctx.rowsSinceYield = 0;
    vTaskDelay(1);
  }
}

// One full canvas row of gray: straight out when the size matches, else area-averaged into
// the output rows it completes (same scheme as PngToBmpConverter).
void processCanvasRow(const uint8_t* gray, const int srcY, GifBmpContext& ctx) {
  if (!ctx.needsScaling) {
    packBmpRow(ctx.bmpRow, ctx.bytesPerRow, ctx.outWidth, *ctx.ditherer, [gray](const int x) { return gray[x]; });
    writeBmpRow(ctx);
    return;
  }

  accumulateAreaRow(gray, ctx.srcWidth, ctx.outWidth, ctx.scaleX_fp, ctx.rowAccum.get(), ctx.rowCount.get());
  const uint32_t srcY_fp = static_cast<uint32_t>(srcY + 1) << 16;
  while (ctx.success && srcY_fp >= ctx.nextOutYSrcStart && ctx.currentOutY < ctx.outHeight) {
    packBmpRow(ctx.bmpRow, ctx.bytesPerRow, ctx.outWidth, *ctx.ditherer, [&ctx](const int x) {
      return static_cast<uint8_t>(ctx.rowCount[x] > 0 ? ctx.rowAccum[x] / ctx.rowCount[x] : 0);
    });
    writeBmpRow(ctx);
    ctx.currentOutY++;
    ctx.nextOutYSrcStart = static_cast<uint32_t>(ctx.currentOutY + 1) * ctx.scaleY_fp;
    if (srcY_fp >= ctx.nextOutYSrcStart) continue;  // upscaling: the next output row reuses these sums
    memset(ctx.rowAccum.get(), 0, static_cast<size_t>(ctx.outWidth) * sizeof(uint32_t));
    memset(ctx.rowCount.get(), 0, static_cast<size_t>(ctx.outWidth) * sizeof(uint16_t));
  }
}

// Canvas rows the frame does not cover are white.
void processGapRowsUpTo(const int canvasY, GifBmpContext& ctx) {
  if (ctx.nextExpectedSrcY >= canvasY) return;
  memset(ctx.grayRow, 255, static_cast<size_t>(ctx.srcWidth));
  while (ctx.success && ctx.nextExpectedSrcY < canvasY && ctx.nextExpectedSrcY < ctx.srcHeight) {
    processCanvasRow(ctx.grayRow, ctx.nextExpectedSrcY, ctx);
    ctx.nextExpectedSrcY++;
  }
}

// The caller's open HalFile rides in as the "filename" (AnimatedGIF hands it to this
// callback untouched), so no global is needed and nothing is reopened.
void* gifOpen(const char* handle, int32_t* size) {
  auto* file = reinterpret_cast<HalFile*>(const_cast<char*>(handle));
  if (!file || !file->seek(0)) return nullptr;
  *size = static_cast<int32_t>(file->size());
  return file;
}

void gifClose(void*) {}  // the caller owns the file

int32_t gifRead(GIFFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) return 0;
  const int32_t bytesRead = file->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t gifSeek(GIFFILE* pFile, int32_t position) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) return -1;
  if (position < 0) position = 0;
  if (position >= pFile->iSize) position = pFile->iSize - 1;
  if (!file->seek(position)) return -1;
  pFile->iPos = position;
  return position;
}

void gifBmpDrawCallback(GIFDRAW* pDraw) {
  auto* ctx = reinterpret_cast<GifBmpContext*>(pDraw->pUser);
  if (!ctx || !ctx->success) return;

  const int canvasY = pDraw->iY + pDraw->y;
  if (canvasY < ctx->nextExpectedSrcY || canvasY >= ctx->srcHeight) {
    ctx->success = false;  // out of order or off the canvas
    return;
  }
  processGapRowsUpTo(canvasY, *ctx);
  if (!ctx->success) return;

  const uint8_t* palette24 = pDraw->pPalette24;
  if (palette24 != nullptr && palette24 != ctx->activePalette24) {
    ctx->activePalette24 = palette24;
    for (int i = 0; i < 256; ++i) {
      const uint8_t* p = &palette24[i * 3];
      ctx->grayPalette[i] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
    }
  }

  memset(ctx->grayRow, 255, static_cast<size_t>(ctx->srcWidth));
  const int endX = pDraw->iX + pDraw->iWidth;
  for (int srcX = pDraw->iX, i = 0; srcX < endX && srcX < ctx->srcWidth; ++srcX, ++i) {
    if (srcX < 0) continue;
    const uint8_t index = pDraw->pPixels[i];
    if (pDraw->ucHasTransparency && index == pDraw->ucTransparent) continue;
    ctx->grayRow[srcX] = ctx->grayPalette[index];
  }

  processCanvasRow(ctx->grayRow, canvasY, *ctx);
  ctx->nextExpectedSrcY = canvasY + 1;
}

}  // namespace

bool GifToBmpConverter::gifFileToBmpStreamInternal(HalFile& gifFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                   bool crop) {
  LOG_DBG("GIF", "Converting GIF to 2-bit BMP (target: %dx%d)", targetWidth, targetHeight);

  GifBasicInfo info;
  if (!GifCommon::readBasicInfo(gifFile, info)) return false;
  if (info.interlaced) {
    LOG_ERR("GIF", "Interlaced GIFs are not supported");
    return false;
  }
  if (info.canvasWidth == 0 || info.canvasHeight == 0 || info.canvasWidth > MAX_WIDTH ||
      info.canvasHeight > MAX_CANVAS_HEIGHT || targetWidth <= 0 || targetHeight <= 0) {
    LOG_ERR("GIF", "Image too large or zero (%ux%u)", info.canvasWidth, info.canvasHeight);
    return false;
  }
  if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(MIN_FREE_HEAP));
    return false;
  }

  auto gif = makeUniqueNoThrow<AnimatedGIF>();
  auto ctxPtr = makeUniqueNoThrow<GifBmpContext>();
  if (!gif || !ctxPtr) {
    LOG_ERR("GIF", "OOM: GIF decoder (%u bytes)", static_cast<unsigned>(sizeof(AnimatedGIF)));
    return false;
  }
  GifBmpContext& ctx = *ctxPtr;
  ctx.bmpOut = &bmpOut;
  ctx.srcWidth = info.canvasWidth;
  ctx.srcHeight = info.canvasHeight;

  ctx.outWidth = ctx.srcWidth;
  ctx.outHeight = ctx.srcHeight;
  if (ctx.srcWidth != targetWidth || ctx.srcHeight != targetHeight) {
    scaleToFit(ctx.srcWidth, ctx.srcHeight, targetWidth, targetHeight, crop, ctx.outWidth, ctx.outHeight);
    ctx.scaleX_fp = (static_cast<uint32_t>(ctx.srcWidth) << 16) / ctx.outWidth;
    ctx.scaleY_fp = (static_cast<uint32_t>(ctx.srcHeight) << 16) / ctx.outHeight;
    ctx.needsScaling = true;
    ctx.nextOutYSrcStart = ctx.scaleY_fp;
  }
  ctx.bytesPerRow = (ctx.outWidth * 2 + 31) / 32 * 4;

  // BMP row and gray row in one block.
  auto rowScratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(ctx.bytesPerRow) + ctx.srcWidth);
  ctx.ditherer = makeUniqueNoThrow<AtkinsonDitherer>(ctx.outWidth);
  if (!rowScratch || !ctx.ditherer || !ctx.ditherer->valid()) {
    LOG_ERR("GIF", "OOM: GIF row buffers");
    return false;
  }
  ctx.bmpRow = rowScratch.get();
  ctx.grayRow = ctx.bmpRow + ctx.bytesPerRow;
  if (ctx.needsScaling) {
    ctx.rowAccum = makeUniqueNoThrow<uint32_t[]>(ctx.outWidth);
    ctx.rowCount = makeUniqueNoThrow<uint16_t[]>(ctx.outWidth);
    if (!ctx.rowAccum || !ctx.rowCount) {
      LOG_ERR("GIF", "OOM: GIF scaling rows");
      return false;
    }
  }

  gif->begin(GIF_PALETTE_RGB888);
  gif->setDrawType(GIF_DRAW_RAW);
  const int rcOpen =
      gif->open(reinterpret_cast<const char*>(&gifFile), gifOpen, gifClose, gifRead, gifSeek, gifBmpDrawCallback);
  const ScopedCleanup cleanup{[&gif]() { gif->close(); }};
  if (rcOpen != 1) {
    LOG_ERR("GIF", "Failed to open GIF (err %d)", gif->getLastError());
    return false;
  }

  bmp_writer::writeHeader2bit(bmpOut, ctx.outWidth, ctx.outHeight);
  int delayMs = 0;
  const int frameState = gif->playFrame(false, &delayMs, &ctx);
  const int err = gif->getLastError();
  if (!ctx.success || (err != GIF_SUCCESS && err != GIF_EMPTY_FRAME)) {
    LOG_ERR("GIF", "GIF decode failed (state %d, err %d)", frameState, err);
    return false;
  }
  processGapRowsUpTo(ctx.srcHeight, ctx);
  if (!ctx.success) return false;
  // The BMP header promised outHeight rows.
  if (ctx.needsScaling ? ctx.currentOutY != ctx.outHeight : ctx.nextExpectedSrcY != ctx.srcHeight) {
    LOG_ERR("GIF", "GIF BMP rows short");
    return false;
  }
  return true;
}

bool GifToBmpConverter::gifFileToBmpStream(HalFile& gifFile, Print& bmpOut, bool crop) {
  // Runtime display dimensions (swapped for portrait cover sizing), as the PNG path.
  return gifFileToBmpStreamInternal(gifFile, bmpOut, display.getDisplayHeight(), display.getDisplayWidth(), crop);
}
