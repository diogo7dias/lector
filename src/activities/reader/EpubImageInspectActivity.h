#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ImageInspectLayout.h"
#include "activities/Activity.h"

class Epub;

// Full-screen viewer for images on the current EPUB page.
// Draws edge-to-edge with no margins or status bar, rotating wide images 90 degrees
// on portrait panels by temporarily switching GfxRenderer's orientation (zero extra
// framebuffer on ESP32-C3). On PSRAM boards (X4 Pro), Confirm or tap toggles 2x zoom
// backed by a PSRAM pixel buffer with button and touch-drag panning.
class EpubImageInspectActivity final : public Activity {
 public:
  struct ImageItem {
    std::string imagePath;
    std::string srcPath;
  };

  EpubImageInspectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<ImageItem> images,
                           int startIndex, Epub* epub);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool appliesNightMode() const override { return true; }

 private:
  bool ensureImageExtracted(const ImageItem& item) const;
  void applyOrientationFor(int srcWidth, int srcHeight);
  void renderFit();
  void stepImage(int delta);
  // What loop() asks render() to paint. Current repaints what is showing.
  enum class Paint : uint8_t { Current, Fit, ToggleZoom, Pan };
  void request(Paint paint, int panDx = 0, int panDy = 0);

#if defined(BOARD_HAS_PSRAM)
  bool buildZoomBuffer();
  void blitZoomed(HalDisplay::RefreshMode refreshMode);
  void toggleZoom();
  void panBy(int dx, int dy);
  void releaseZoomBuffer();
#endif

  std::vector<ImageItem> images;
  int currentIndex = 0;
  Epub* epub = nullptr;
  GfxRenderer::Orientation originalOrientation = GfxRenderer::Orientation::Portrait;
  int origScreenWidth = 0;
  int origScreenHeight = 0;
  image_inspect::Rect fitRect{};
  bool decodedOk = false;
  Paint pendingPaint = Paint::Fit;
  int pendingPanX = 0;
  int pendingPanY = 0;

#if defined(BOARD_HAS_PSRAM)
  bool zoomed = false;
  image_inspect::ZoomView zoomView{};
  int zoomStride = 0;
  std::unique_ptr<uint8_t[]> zoomBuffer;
  bool dragActive = false;
  bool dragIgnored = false;
  int dragStartX = 0;
  int dragStartY = 0;
  int dragLastX = 0;
  int dragLastY = 0;
#endif
};
