#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace image_inspect {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct ZoomView {
  int zoomedW = 0;
  int zoomedH = 0;
  int panX = 0;
  int panY = 0;
  int offsetX = 0;
  int offsetY = 0;
};

// Wide images on a portrait panel rotate 90 degrees so their long side matches the
// panel's long axis. Already-landscape panels keep wide images upright.
inline bool shouldRotateWide(const int srcWidth, const int srcHeight, const int screenWidth, const int screenHeight) {
  return srcWidth > 0 && srcHeight > 0 && srcWidth > srcHeight && screenHeight > screenWidth;
}

inline uint8_t rotateOrientation90(const uint8_t orientation) { return static_cast<uint8_t>((orientation + 1) & 3); }

// Aspect-preserving fit to the full panel (no margins, no status bar), centered.
inline Rect fitToPanel(const int srcWidth, const int srcHeight, const int panelWidth, const int panelHeight) {
  if (srcWidth <= 0 || srcHeight <= 0 || panelWidth <= 0 || panelHeight <= 0) return {};
  const float scaleX = static_cast<float>(panelWidth) / static_cast<float>(srcWidth);
  const float scaleY = static_cast<float>(panelHeight) / static_cast<float>(srcHeight);
  const float scale = std::min(scaleX, scaleY);
  const int w = std::clamp(static_cast<int>(std::lround(static_cast<float>(srcWidth) * scale)), 1, panelWidth);
  const int h = std::clamp(static_cast<int>(std::lround(static_cast<float>(srcHeight) * scale)), 1, panelHeight);
  return Rect{(panelWidth - w) / 2, (panelHeight - h) / 2, w, h};
}

// 2x zoom viewport math with pan clamping. When an axis fits inside the panel even at 2x,
// pan stays 0 and offset centers the zoomed axis.
inline ZoomView makeZoomedView(const int fitWidth, const int fitHeight, const int panelWidth, const int panelHeight,
                               const int panX, const int panY) {
  ZoomView v;
  v.zoomedW = std::max(0, fitWidth * 2);
  v.zoomedH = std::max(0, fitHeight * 2);
  const int maxPanX = std::max(0, v.zoomedW - panelWidth);
  const int maxPanY = std::max(0, v.zoomedH - panelHeight);
  v.panX = std::clamp(panX, 0, maxPanX);
  v.panY = std::clamp(panY, 0, maxPanY);
  v.offsetX = v.zoomedW < panelWidth ? (panelWidth - v.zoomedW) / 2 : 0;
  v.offsetY = v.zoomedH < panelHeight ? (panelHeight - v.zoomedH) / 2 : 0;
  return v;
}

inline ZoomView centerZoomedView(const int fitWidth, const int fitHeight, const int panelWidth, const int panelHeight) {
  const int maxPanX = std::max(0, fitWidth * 2 - panelWidth);
  const int maxPanY = std::max(0, fitHeight * 2 - panelHeight);
  return makeZoomedView(fitWidth, fitHeight, panelWidth, panelHeight, maxPanX / 2, maxPanY / 2);
}

inline int hitTestRect(const Rect* rects, const int count, const int x, const int y) {
  if (!rects || count <= 0) return -1;
  for (int i = 0; i < count; ++i) {
    const Rect& r = rects[i];
    if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
      return i;
    }
  }
  return -1;
}

// Detects two taps on the same image within MAX_INTERVAL_MS and MAX_DISTANCE_PX.
class DoubleTapTracker {
 public:
  static constexpr unsigned long MAX_INTERVAL_MS = 500;
  static constexpr int MAX_DISTANCE_PX = 48;

  void reset() {
    armed = false;
    lastIndex = -1;
  }

  bool recordTap(const unsigned long nowMs, const int x, const int y, const int imageIndex) {
    if (imageIndex < 0) {
      reset();
      return false;
    }
    const int dx = x - lastX;
    const int dy = y - lastY;
    const bool match = armed && imageIndex == lastIndex && (nowMs - lastMs) <= MAX_INTERVAL_MS &&
                       (dx * dx + dy * dy) <= MAX_DISTANCE_PX * MAX_DISTANCE_PX;
    if (match) {
      reset();
      return true;
    }
    armed = true;
    lastMs = nowMs;
    lastX = x;
    lastY = y;
    lastIndex = imageIndex;
    return false;
  }

 private:
  bool armed = false;
  unsigned long lastMs = 0;
  int lastX = 0;
  int lastY = 0;
  int lastIndex = -1;
};

}  // namespace image_inspect
