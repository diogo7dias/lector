#include "EpubImageInspectActivity.h"

#include <Epub.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubImageInspectActivity::EpubImageInspectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   std::vector<ImageItem> images, const int startIndex, Epub* epub)
    : Activity("EpubImageInspect", renderer, mappedInput),
      images(std::move(images)),
      currentIndex(startIndex),
      epub(epub) {
  if (this->images.empty()) {
    currentIndex = 0;
  } else if (currentIndex < 0 || currentIndex >= static_cast<int>(this->images.size())) {
    currentIndex = 0;
  }
}

void EpubImageInspectActivity::onEnter() {
  Activity::onEnter();
  originalOrientation = renderer.getOrientation();
  origScreenWidth = renderer.getScreenWidth();
  origScreenHeight = renderer.getScreenHeight();
  requestUpdate();
}

void EpubImageInspectActivity::onExit() {
#if defined(BOARD_HAS_PSRAM)
  releaseZoomBuffer();
#endif
  renderer.setDitherImagesInBw(false);
  renderer.setOrientation(originalOrientation);
  Activity::onExit();
}

bool EpubImageInspectActivity::ensureImageExtracted(const ImageItem& item) const {
  if (item.imagePath.empty()) return false;
  if (Storage.exists(item.imagePath.c_str())) return true;
  if (item.srcPath.empty() || !epub) return false;
  return epub->extractItemToFile(item.srcPath.c_str(), item.imagePath.c_str());
}

void EpubImageInspectActivity::applyOrientationFor(const int srcWidth, const int srcHeight) {
  if (image_inspect::shouldRotateWide(srcWidth, srcHeight, origScreenWidth, origScreenHeight)) {
    const uint8_t rotated = image_inspect::rotateOrientation90(static_cast<uint8_t>(originalOrientation));
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(rotated));
  } else {
    renderer.setOrientation(originalOrientation);
  }
}

void EpubImageInspectActivity::renderFit() {
#if defined(BOARD_HAS_PSRAM)
  releaseZoomBuffer();
  zoomed = false;
#endif
  decodedOk = false;
  fitRect = {};
  renderer.setOrientation(originalOrientation);

  if (images.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_FILE_OPEN_FAILED), true);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  const ImageItem& item = images[currentIndex];
  auto* const decoder = ImageDecoderFactory::getDecoder(item.imagePath);
  ImageDimensions dims{};
  if (!ensureImageExtracted(item) || !decoder || !decoder->getDimensions(item.imagePath, dims) || dims.width <= 0 ||
      dims.height <= 0) {
    LOG_ERR("IMGV", "Failed to open image for inspection: %s", item.imagePath.c_str());
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_FILE_OPEN_FAILED), true);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  applyOrientationFor(dims.width, dims.height);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  fitRect = image_inspect::fitToPanel(dims.width, dims.height, screenW, screenH);

  renderer.clearScreen();
  // 1-bit Bayer dither in the BW framebuffer: clean detail with zero persistent RAM on C3.
  renderer.setDitherImagesInBw(true);
  RenderConfig cfg{fitRect.x, fitRect.y, fitRect.width, fitRect.height};
  cfg.useDithering = true;
  cfg.useExactDimensions = true;
  decodedOk = decoder->decodeToFramebuffer(item.imagePath, renderer, cfg);
  renderer.setDitherImagesInBw(false);

  if (!decodedOk) {
    LOG_ERR("IMGV", "Decode failed: %s", item.imagePath.c_str());
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, screenH / 2, tr(STR_FILE_OPEN_FAILED), true);
  } else {
    renderer.preserveImagePolarity(fitRect.x, fitRect.y, fitRect.width, fitRect.height);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void EpubImageInspectActivity::stepImage(const int delta) {
  if (images.size() <= 1) return;
  const int next = currentIndex + delta;
  if (next < 0 || next >= static_cast<int>(images.size())) return;
  currentIndex = next;
  request(Paint::Fit);
}

#if defined(BOARD_HAS_PSRAM)
void EpubImageInspectActivity::releaseZoomBuffer() {
  zoomBuffer.reset();
  zoomStride = 0;
  zoomView = {};
  dragActive = false;
  dragIgnored = false;
}

bool EpubImageInspectActivity::buildZoomBuffer() {
  if (!decodedOk || images.empty()) return false;
  const ImageItem& item = images[currentIndex];
  auto* const decoder = ImageDecoderFactory::getDecoder(item.imagePath);
  if (!decoder) return false;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const image_inspect::ZoomView view = image_inspect::centerZoomedView(fitRect.width, fitRect.height, screenW, screenH);
  if (view.zoomedW <= 0 || view.zoomedH <= 0) return false;

  const int stride = (view.zoomedW + 7) / 8;
  const size_t totalBytes = static_cast<size_t>(stride) * static_cast<size_t>(view.zoomedH);
  auto buf = makeUniqueNoThrow<uint8_t[]>(totalBytes);
  if (!buf) {
    LOG_ERR("IMGV", "OOM zoom buffer: %u bytes", static_cast<unsigned>(totalBytes));
    return false;
  }
  memset(buf.get(), 0, totalBytes);

  // Decode the 2x image in screen-sized tiles via the existing decoder and pack 1-bit
  // ink pixels into PSRAM so subsequent pan operations blit in milliseconds.
  renderer.setDitherImagesInBw(true);
  bool ok = true;
  for (int tileY = 0; tileY < view.zoomedH && ok; tileY += screenH) {
    const int tileH = std::min(screenH, view.zoomedH - tileY);
    for (int tileX = 0; tileX < view.zoomedW && ok; tileX += screenW) {
      const int tileW = std::min(screenW, view.zoomedW - tileX);
      renderer.clearScreen();
      RenderConfig cfg{-tileX, -tileY, view.zoomedW, view.zoomedH};
      cfg.useDithering = true;
      cfg.useExactDimensions = true;
      if (!decoder->decodeToFramebuffer(item.imagePath, renderer, cfg)) {
        ok = false;
        break;
      }
      for (int y = 0; y < tileH; ++y) {
        uint8_t* dstRow = buf.get() + static_cast<size_t>(tileY + y) * static_cast<size_t>(stride);
        for (int x = 0; x < tileW; ++x) {
          if (renderer.isPixelBlack(x, y)) {
            const int dstX = tileX + x;
            dstRow[dstX >> 3] |= static_cast<uint8_t>(0x80u >> (dstX & 7));
          }
        }
      }
    }
  }
  renderer.setDitherImagesInBw(false);

  if (!ok) {
    LOG_ERR("IMGV", "Failed to decode 2x tiles: %s", item.imagePath.c_str());
    return false;
  }

  zoomBuffer = std::move(buf);
  zoomStride = stride;
  zoomView = view;
  return true;
}

void EpubImageInspectActivity::blitZoomed(const HalDisplay::RefreshMode refreshMode) {
  if (!zoomBuffer) return;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int visW = std::min(screenW - zoomView.offsetX, zoomView.zoomedW - zoomView.panX);
  const int visH = std::min(screenH - zoomView.offsetY, zoomView.zoomedH - zoomView.panY);

  renderer.clearScreen();
  for (int y = 0; y < visH; ++y) {
    const uint8_t* srcRow = zoomBuffer.get() + static_cast<size_t>(zoomView.panY + y) * static_cast<size_t>(zoomStride);
    const int outY = zoomView.offsetY + y;
    for (int x = 0; x < visW; ++x) {
      const int srcX = zoomView.panX + x;
      if (srcRow[srcX >> 3] & static_cast<uint8_t>(0x80u >> (srcX & 7))) {
        renderer.drawPixel(zoomView.offsetX + x, outY, true);
      }
    }
  }
  renderer.preserveImagePolarity(zoomView.offsetX, zoomView.offsetY, visW, visH);
  renderer.displayBuffer(refreshMode);
}

void EpubImageInspectActivity::toggleZoom() {
  if (!decodedOk) return;
  if (zoomed) {
    renderFit();
    return;
  }
  if (!buildZoomBuffer()) {
    renderFit();
    return;
  }
  zoomed = true;
  LOG_INF("IMGV", "Zoom 2x (%dx%d, pan %d,%d)", zoomView.zoomedW, zoomView.zoomedH, zoomView.panX, zoomView.panY);
  blitZoomed(HalDisplay::HALF_REFRESH);
}

void EpubImageInspectActivity::panBy(const int dx, const int dy) {
  if (!zoomed || !zoomBuffer) return;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const auto next = image_inspect::makeZoomedView(fitRect.width, fitRect.height, screenW, screenH, zoomView.panX + dx,
                                                  zoomView.panY + dy);
  if (next.panX == zoomView.panX && next.panY == zoomView.panY) return;
  zoomView = next;
  LOG_INF("IMGV", "Pan (%d,%d)", zoomView.panX, zoomView.panY);
  blitZoomed(HalDisplay::FAST_REFRESH);
}
#endif

// loop() only records what to paint. The paint itself runs here, on the render task, like
// every other screen: ActivityManager has set the night mode polarity for it, and a pass asked
// for by something else (the light panel closing over the image) repaints what was showing.
void EpubImageInspectActivity::request(const Paint paint, const int panDx, const int panDy) {
  if (paint == Paint::Pan) {
    if (pendingPaint == Paint::Fit || pendingPaint == Paint::ToggleZoom) return;
    pendingPanX += panDx;
    pendingPanY += panDy;
  }
  pendingPaint = paint;
  requestUpdate();
}

void EpubImageInspectActivity::render(RenderLock&&) {
  const Paint paint = std::exchange(pendingPaint, Paint::Current);
  const int panDx = std::exchange(pendingPanX, 0);
  const int panDy = std::exchange(pendingPanY, 0);
#if defined(BOARD_HAS_PSRAM)
  if (paint == Paint::ToggleZoom) {
    toggleZoom();
    return;
  }
  if (paint == Paint::Pan) {
    panBy(panDx, panDy);
    return;
  }
  if (paint == Paint::Current && zoomed) {
    blitZoomed(HalDisplay::HALF_REFRESH);
    return;
  }
#else
  (void)panDx;
  (void)panDy;
#endif
  renderFit();
}

void EpubImageInspectActivity::loop() {
  Activity::loop();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
    finish();
    return;
  }

#if defined(BOARD_HAS_PSRAM)
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    request(Paint::ToggleZoom);
    return;
  }

  if (zoomed) {
    // Touch drag panning while zoomed on X4 Pro: track finger while held and pan on lift/drag.
    if (mappedInput.hasTouch()) {
      int tx = 0;
      int ty = 0;
      if (mappedInput.isScreenTouchHeld(tx, ty)) {
        // Leave contacts starting in the left-edge Back gesture band untouched for the
        // entire contact so swiping Back still exits while zoomed.
        const int backEdgeX = renderer.getScreenWidth() * 12 / 100;
        if (!dragActive && !dragIgnored) {
          if (tx <= backEdgeX) {
            dragIgnored = true;
            return;
          }
          dragActive = true;
          dragStartX = dragLastX = tx;
          dragStartY = dragLastY = ty;
        } else if (dragActive) {
          dragLastX = tx;
          dragLastY = ty;
          const int dx = dragStartX - dragLastX;
          const int dy = dragStartY - dragLastY;
          if (std::abs(dx) >= 32 || std::abs(dy) >= 32) {
            gpio.suppressTouchContact();
            request(Paint::Pan, dx, dy);
            dragStartX = dragLastX;
            dragStartY = dragLastY;
          }
        }
        return;
      }
      dragActive = false;
      dragIgnored = false;
      float nxs = 0.0f, nys = 0.0f, nxe = 0.0f, nye = 0.0f;
      if (gpio.wasSwipe(nxs, nys, nxe, nye)) {
        int sx = 0, sy = 0, ex = 0, ey = 0;
        renderer.tapToLogical(nxs, nys, sx, sy);
        renderer.tapToLogical(nxe, nye, ex, ey);
        request(Paint::Pan, sx - ex, sy - ey);
        return;
      }
      if (mappedInput.wasScreenTapped(tx, ty)) {
        request(Paint::ToggleZoom);
        return;
      }
    }

    const int stepX = std::max(40, renderer.getScreenWidth() / 4);
    const int stepY = std::max(40, renderer.getScreenHeight() / 4);
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      request(Paint::Pan, -stepX, 0);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      request(Paint::Pan, stepX, 0);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      request(Paint::Pan, 0, -stepY);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      request(Paint::Pan, 0, stepY);
      return;
    }
    return;
  }

  if (mappedInput.hasTouch()) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      request(Paint::ToggleZoom);
      return;
    }
  }
#endif

  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    stepImage(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    stepImage(1);
    return;
  }
}

// The reader's side of the viewer, kept here with the screen it opens.
void EpubReaderActivity::openImageInspector(const int startIndex) {
  if (currentPageImages.empty()) return;
  std::vector<EpubImageInspectActivity::ImageItem> items;
  items.reserve(currentPageImages.size());
  for (const auto& img : currentPageImages) {
    items.push_back(EpubImageInspectActivity::ImageItem{img.imagePath, img.srcPath});
  }
  // Drop the section while inspecting so C3 has maximum contiguous DRAM for full-panel
  // JPEG/PNG decode; returning restores the cached page via the same path as chapter select.
  {
    RenderLock lock(*this);
    cacheSectionPosition();
    section.reset();
  }
  auto viewer =
      makeUniqueNoThrow<EpubImageInspectActivity>(renderer, mappedInput, std::move(items), startIndex, epub.get());
  if (!viewer) {
    LOG_ERR("ERS", "OOM: EpubImageInspectActivity");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(viewer), [this](const ActivityResult&) { requestUpdate(); });
}
