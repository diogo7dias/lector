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

bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path, const bool grayscale,
                          const HalDisplay::RefreshMode oneBitRefresh) {
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) {
    return false;
  }

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
  std::unique_ptr<uint8_t[]> frame;
  if (grayscale) {
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
        if (!grayscale && pixelValue != 0 && pixelValue != 3) {
          // Standalone 1-bit render: the plain BW pass collapses every non-white
          // level to solid black, which turns a light wallpaper into a black blob.
          // Ordered-dither the two mid levels (85 / 170) with a 2x2 Bayer matrix so
          // tone survives in pure B&W; pure black (0) and pure white (3) stay solid.
          static const uint8_t kBayer2[2][2] = {{0, 2}, {3, 1}};
          pixelValue = (pixelValue > kBayer2[row & 1][col & 1]) ? 3 : 0;
        }
        pw.writePixel(x + col, pixelValue);
      }
    }
    return true;
  };

  // OEM 3-pass grayscale, mirroring SleepActivity::renderBitmapSleepScreen: BW
  // silhouette base, then the LSB and MSB grayscale planes, then composite.
  LOG_INF("SLP", "pxc decode BW begin");
  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decode()) return false;
  LOG_INF("SLP", "pxc decode BW end");

  if (!grayscale) {
    // 1-bit fast path: a single refresh of the dithered silhouette, skipping the
    // LSB/MSB planes and the grayscale composite. Refresh mode is caller-selected.
    LOG_INF("SLP", "pxc displayBuffer 1bit begin");
    renderer.displayBuffer(oneBitRefresh);
    LOG_INF("SLP", "pxc displayBuffer 1bit end");
    return true;
  }

  // Clean base paint. displayGrayscaleBase's HALF waveform is the exact base the
  // gray-nudge LUT is calibrated against (see renderBitmapSleepScreen).
  LOG_INF("SLP", "pxc grayBase begin");
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  LOG_INF("SLP", "pxc grayBase end");

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  LOG_INF("SLP", "pxc decode LSB begin");
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  LOG_INF("SLP", "pxc copyLSB begin");
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  LOG_INF("SLP", "pxc decode MSB begin");
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  LOG_INF("SLP", "pxc copyMSB begin");
  renderer.copyGrayscaleMsbBuffers();

  LOG_INF("SLP", "pxc grayBuffer begin");
  renderer.displayGrayBuffer();
  LOG_INF("SLP", "pxc grayBuffer end");
  renderer.setRenderMode(GfxRenderer::BW);
  LOG_INF("SLP", "pxc done gray");
  return true;
}
