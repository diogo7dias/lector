#pragma once

#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

// An image with an alpha channel (a 32-bit BMP or a PNG) to draw over the page the reader
// left, or over the book's cover: the Transparent and Cover + Transparent sleep faces.
// Ported from CrossPoint 2.0.0, which keeps these apart from the full-screen wallpapers.
//
// prepare() finds a source and checks it, drawing nothing; draw() then paints it into the
// framebuffer in whatever render mode the renderer is in (BW, or one of the two grey planes),
// so the caller can run the passes of the grayscale pipeline over its own picture.
class SleepOverlay {
 public:
  // /sleep-overlay.bmp, /sleep-overlay.png, then a random file from /.sleep-overlay and
  // /sleep-overlay. False when none is usable (missing, unreadable, or a BMP with no alpha).
  bool prepare(const GfxRenderer& renderer);
  // One pass. False on a read or decode error.
  bool draw(GfxRenderer& renderer);

 private:
  struct Placement {
    int x = 0;
    int y = 0;
    float cropX = 0.0f;
    float cropY = 0.0f;
  };
  struct BmpInfo {
    int width = 0;
    int height = 0;
    bool topDown = false;
    uint32_t dataOffset = 0;
    uint32_t rowBytes = 0;
  };

  bool prepareFile(const GfxRenderer& renderer, const std::string& path);
  bool drawBmp(const GfxRenderer& renderer);

  std::string path;
  bool isPng = false;
  Placement placement;
  // PNG
  RenderConfig config{};
  // BMP: kept open between passes
  HalFile file;
  BmpInfo bmp;
  std::unique_ptr<uint8_t[]> row;
};
