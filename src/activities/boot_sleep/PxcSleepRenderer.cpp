// Hot translation unit: compiled -O2 instead of the global -Os. The inner decode
// loops here dominate render time on the flash-cache-starved ESP32-C3; the size
// cost is confined to this file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "PxcSleepRenderer.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdint>
#include <cstdlib>

#include "Epub/converters/DirectPixelWriter.h"
#include "PxcDither.h"
#include "SleepGrayscaleBase.h"
#include "SleepTiming.h"

bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path, const bool grayscale,
                          const HalDisplay::RefreshMode oneBitRefresh, void (*const overlay)(GfxRenderer&),
                          const PxcRenderOptions* const opts) {
#ifdef LECTOR_LOCK_LAB
  // A default-constructed struct is the shipped behaviour, so a lab build with no options
  // set renders exactly what a release build renders.
  static const PxcRenderOptions kShipped;
  const PxcRenderOptions& o = (opts != nullptr) ? *opts : kShipped;
#else
  (void)opts;
#endif
  HalFile file;
  const uint32_t openStartMs = millis();
  // Storage.open, not openFileForRead: the latter calls exists() and then open(), and
  // each is a full FAT directory walk. In a wallpaper folder that is 1271 ms apiece,
  // which is why a wallpaper open measured 2543 ms. One walk answers it.
  file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) {
    // INF, not ERR: the sleep faces probe for an optional /sleep.pxc on every lock, and
    // most cards do not have one. A miss here is the normal case, not a fault.
    LOG_INF("SLP", "pxc open failed: %s", path.c_str());
    return false;
  }
  // Opening by path is a FAT lookup: a linear walk of the directory. In a wallpaper
  // folder with thousands of files that is not free, and the gap between picking a name
  // and reading its header measured 2609 ms with nothing to explain it.
  LOG_INF("SLP", "pxc open in %ums", static_cast<unsigned>(millis() - openStartMs));
  SleepTiming::mark("pxcopen");

  uint16_t pxcWidth = 0, pxcHeight = 0;
  if (file.read(&pxcWidth, 2) != 2 || file.read(&pxcHeight, 2) != 2) {
    LOG_ERR("SLP", "pxc header read failed: %s", path.c_str());
    return false;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  // Wallpapers are authored at the exact panel size; allow 1px rounding slack.
  if (abs(static_cast<int>(pxcWidth) - screenWidth) > 1 || abs(static_cast<int>(pxcHeight) - screenHeight) > 1) {
    LOG_ERR("SLP", "pxc size %dx%d != screen %dx%d", pxcWidth, pxcHeight, screenWidth, screenHeight);
    return false;
  }
  LOG_INF("SLP", "pxc %dx%d ok grayscale=%d", pxcWidth, pxcHeight, static_cast<int>(grayscale));

  const size_t dataOffset = file.position();
  const int bytesPerRow = (pxcWidth + 3) / 4;  // 2bpp, 4 px/byte

  // Read the whole 2bpp payload once so the three grayscale planes decode from RAM
  // instead of re-reading it from SD three times. At low sleep-entry heap this
  // ~90KB block often will not fit — that is expected, not an error; when it does
  // not fit we fall back to the per-pass row-batch SD reads below. The 1-bit path
  // decodes exactly once, so the cache buys nothing there — skip the attempt.
  const size_t payloadBytes = static_cast<size_t>(bytesPerRow) * pxcHeight;
  bool wantFrameCache = grayscale;
#ifdef LECTOR_LOCK_LAB
  // Turning the cache off is how the lab measures what the three re-reads actually cost
  // on a board where the payload does fit, which is every X4 Pro (8MB PSRAM) and no C3.
  wantFrameCache = grayscale && o.wholeFileCache;
#endif
  std::unique_ptr<uint8_t[]> frame;
  if (wantFrameCache) {
    frame = makeUniqueNoThrow<uint8_t[]>(payloadBytes);
    if (frame) {
      if (!file.seek(dataOffset) || file.read(frame.get(), payloadBytes) != static_cast<int>(payloadBytes)) {
        frame.reset();
      }
    }
  }
  const uint8_t* const frameData = frame ? frame.get() : nullptr;

  // Batch rows into ~4KB when heap allows, else fall back to a single row. At
  // sleep entry the heap is low/fragmented, so the single-row floor keeps this
  // from OOM-bricking (build is -fno-exceptions: a failed alloc must be caught
  // here, never thrown).
  int rowsPerRead = 4096 / bytesPerRow;
#ifdef LECTOR_LOCK_LAB
  if (o.rowsPerRead != 0) rowsPerRead = o.rowsPerRead;
#endif
  if (rowsPerRead < 1) rowsPerRead = 1;
  if (rowsPerRead > pxcHeight) rowsPerRead = pxcHeight;
  auto readBuffer = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(rowsPerRead) * bytesPerRow);
  if (!readBuffer) {
    rowsPerRead = 1;
    readBuffer = makeUniqueNoThrow<uint8_t[]>(bytesPerRow);
  }
  if (!readBuffer) {
    LOG_ERR("SLP", "pxc row buffer alloc failed");
    return false;
  }
  uint8_t* const readBufferData = readBuffer.get();

  // The image is full-screen, so the origin is (0,0); no centering/scaling.
  const int x = 0, y = 0;

  // Hoisted out of the per-pixel loop: one load here instead of 384000 of them. On the
  // release path the dither mode is a literal, so the switch inside ditherMid folds away.
#ifdef LECTOR_LOCK_LAB
  const uint8_t* const levelMap = o.levelMap;
  const bool invert = o.invert;
  const PxcRenderOptions::Dither ditherMode = o.dither;
#else
  constexpr PxcRenderOptions::Dither ditherMode = PxcRenderOptions::BAYER2;
#endif

  // Decode the whole frame into the CURRENT render mode. Re-seekable so it can be
  // replayed once per grayscale plane. Returns false on a read/seek error.
  auto decode = [&]() -> bool {
    if (!frameData && !file.seek(dataOffset)) return false;
    DirectPixelWriter pw;
    pw.init(renderer);
    int rowsInBuffer = 0, bufferRow = 0;
    for (int row = 0; row < pxcHeight; row++) {
      const uint8_t* rowBuffer;
      if (frameData) {
        rowBuffer = frameData + static_cast<size_t>(row) * bytesPerRow;
      } else {
        if (bufferRow >= rowsInBuffer) {
          const int toRead = (pxcHeight - row < rowsPerRead) ? (pxcHeight - row) : rowsPerRead;
          const size_t bytes = static_cast<size_t>(toRead) * bytesPerRow;
          if (file.read(readBufferData, bytes) != static_cast<int>(bytes)) {
            LOG_ERR("SLP", "pxc read error at row %d", row);
            return false;
          }
          rowsInBuffer = toRead;
          bufferRow = 0;
        }
        rowBuffer = readBufferData + static_cast<size_t>(bufferRow) * bytesPerRow;
        bufferRow++;
      }
      pw.beginRow(y + row);
      int colStart, colEnd;
      pw.bandColRange(x, pxcWidth, colStart, colEnd);
      for (int col = colStart; col < colEnd; col++) {
        const int byteIdx = col >> 2;            // col / 4
        const int bitShift = 6 - (col & 3) * 2;  // MSB first within byte
        uint8_t pixelValue = (rowBuffer[byteIdx] >> bitShift) & 0x03;
#ifdef LECTOR_LOCK_LAB
        // Tone first, then polarity, then dither: the mask has to see the levels the
        // panel will actually be asked to draw, or it dithers the wrong two.
        pixelValue = levelMap[pixelValue];
        if (invert) pixelValue = 3 - pixelValue;
#endif
        if (!grayscale && pixelValue != 0 && pixelValue != 3) {
          // Standalone 1-bit render: the plain BW pass collapses every non-white
          // level to solid black, which turns a light wallpaper into a black blob.
          // Ordered-dither the two mid levels (85 / 170) so tone survives in pure B&W;
          // pure black (0) and pure white (3) stay solid.
          pixelValue = pxcdither::ditherMid(ditherMode, pixelValue, row, col);
        }
        pw.writePixel(x + col, pixelValue);
      }
    }
    return true;
  };

  // Stage markers carry the elapsed time since the render started, so a serial log
  // shows not just where a sleep stalls but whether it stalled on the panel: a step
  // that jumps ~30 s is the SDK's BUSY-wait ceiling expiring. Drop these once the X4
  // grayscale sleep path is device-confirmed.
  const uint32_t startMs = millis();
  const auto stage = [startMs](const char* name) {
    LOG_INF("SLP", "pxc %s @%ums", name, static_cast<unsigned>(millis() - startMs));
  };

  // OEM 3-pass grayscale, mirroring SleepActivity::renderBitmapSleepScreen: BW
  // silhouette base, then the LSB and MSB grayscale planes, then composite.
  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decode()) return false;
  // The overlay draws into the same framebuffer as the wallpaper, before the
  // refresh, so the two land together (no intermediate wallpaper-only flash).
  if (overlay != nullptr) overlay(renderer);
  stage("decode BW");
  // Same split the bitmap face reports: card and decode above, panel below.
  SleepTiming::mark("decode");

  if (!grayscale) {
    // 1-bit fast path: a single refresh of the dithered silhouette, skipping the
    // LSB/MSB planes and the grayscale composite. Refresh mode is caller-selected.
    renderer.displayBuffer(oneBitRefresh);
    stage("displayBuffer 1bit");
    return true;
  }

  // Clean base paint. displayGrayscaleBase's HALF waveform is the exact base the
  // gray-nudge LUT is calibrated against (see renderBitmapSleepScreen). On the X4
  // that waveform also powers the panel rails down, which is why the driver config
  // enables grayPowerUpFirst (src/platform/LectorSsd1677Config.cpp).
  HalDisplay::RefreshMode grayBase = sleepGrayscaleBaseRefresh();
#ifdef LECTOR_LOCK_LAB
  if (o.grayBaseRefresh >= 0) grayBase = static_cast<HalDisplay::RefreshMode>(o.grayBaseRefresh);
  if (o.passes != PxcRenderOptions::PLANES_ONLY) {
    renderer.displayGrayscaleBase(grayBase);
  }
#else
  renderer.displayGrayscaleBase(grayBase);
#endif
  stage("grayBase");
  SleepTiming::mark("graybase");

#ifdef LECTOR_LOCK_LAB
  if (o.passes == PxcRenderOptions::BASE_ONLY) {
    // The base paint alone, so whatever is left of the render's cost belongs to the two
    // planes and the composite rather than to the waveform.
    renderer.setRenderMode(GfxRenderer::BW);
    stage("base only done");
    return true;
  }
#endif

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  // Re-drawn per plane for the same reason the wallpaper is: a plane pass only
  // carries the pixels written during that pass.
  if (overlay != nullptr) overlay(renderer);
  renderer.copyGrayscaleLsbBuffers();
  stage("copyLSB");
  SleepTiming::mark("lsb");

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  if (overlay != nullptr) overlay(renderer);
  renderer.copyGrayscaleMsbBuffers();
  stage("copyMSB");
  SleepTiming::mark("msb");

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  stage("grayBuffer done");
  return true;
}
