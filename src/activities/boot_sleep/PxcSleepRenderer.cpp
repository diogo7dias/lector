#include "PxcSleepRenderer.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdint>
#include <cstdlib>

#include "Epub/converters/DirectPixelWriter.h"
#include "SleepInfoOverlay.h"

bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path, const std::function<void()>& extraOverlay,
                          bool drawInfoOverlay, bool grayscale) {
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

  const size_t dataOffset = file.position();
  const int bytesPerRow = (pxcWidth + 3) / 4;  // 2bpp, 4 px/byte

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
    if (!file.seek(dataOffset)) return false;
    DirectPixelWriter pw;
    pw.init(renderer);
    int rowsInBuffer = 0, bufferRow = 0;
    for (int row = 0; row < pxcHeight; row++) {
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
      const uint8_t* rowBuffer = readBufferData + static_cast<size_t>(bufferRow) * bytesPerRow;
      bufferRow++;
      pw.beginRow(y + row);
      int colStart, colEnd;
      pw.bandColRange(x, pxcWidth, colStart, colEnd);
      for (int col = colStart; col < colEnd; col++) {
        const int byteIdx = col >> 2;            // col / 4
        const int bitShift = 6 - (col & 3) * 2;  // MSB first within byte
        uint8_t pixelValue = (rowBuffer[byteIdx] >> bitShift) & 0x03;
        if (!grayscale && pixelValue != 0 && pixelValue != 3) {
          // Standalone 1-bit render (unlock screen): the plain BW pass collapses
          // every non-white level to solid black, which turns a light-background
          // wallpaper into a black blob that reads as "gone white". Ordered-dither
          // the two mid levels (85 / 170) with a 2x2 Bayer matrix so tone survives
          // in pure black & white; pure black (0) and pure white (3) stay solid.
          static const uint8_t kBayer2[2][2] = {{0, 2}, {3, 1}};
          pixelValue = (pixelValue > kBayer2[row & 1][col & 1]) ? 3 : 0;
        }
        pw.writePixel(x + col, pixelValue);
      }
    }
    return true;
  };

  // OEM 3-pass grayscale, mirroring SleepActivity::renderBitmapSleepScreen:
  // BW silhouette base, then the LSB and MSB grayscale planes, then composite.
  // The info overlay (filename / favorite badge) is redrawn in every pass so it
  // composites solid, exactly like the wallpaper itself.
  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decode()) return false;
  if (drawInfoOverlay) drawSleepInfoOverlay(renderer, path);
  if (extraOverlay) extraOverlay();

  if (!grayscale) {
    // 1-bit fast path: a single BW refresh of the silhouette + overlays, skipping
    // the LSB/MSB grayscale planes and the grayscale composite. Used by the unlock
    // banner screen, where wake speed matters more than a full grayscale wallpaper
    // (the sleep screen itself still renders full grayscale).
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return true;
  }

  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  if (drawInfoOverlay) drawSleepInfoOverlay(renderer, path);
  if (extraOverlay) extraOverlay();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!decode()) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  if (drawInfoOverlay) drawSleepInfoOverlay(renderer, path);
  if (extraOverlay) extraOverlay();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}
