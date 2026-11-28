#pragma once

#include <algorithm>
#include <cstdint>

// Status bar tap cycle on touch boards (X4 Pro): tapping the status bar band
// cycles Page N/M -> N left -> Book % -> Hidden -> Page N/M. Pure state and
// hit-test rules so they are host-tested without GfxRenderer or input state.
namespace status_bar_tap {

// Minimum touch band height along an active (or hidden recovery) status edge.
constexpr int kMinTapBandPx = 40;

struct State {
  uint8_t enabled;     // 0 = hidden, 1 = visible
  uint8_t pageFormat;  // 0 = N/M, 1 = N left, 2 = Book %
};

struct StepResult {
  State next;
  bool layoutChanged;  // true only when visibility flips and viewport height changes
};

inline StepResult nextState(const State current) {
  if (current.enabled == 0) {
    return {{1, 0}, true};
  }
  if (current.pageFormat == 0) {
    return {{1, 1}, false};
  }
  if (current.pageFormat == 1) {
    return {{1, 2}, false};
  }
  return {{0, 0}, true};
}

// True when logical tap coordinate `y` lands inside an active status bar band, or inside
// the bottom edge recovery band while the status bar is hidden.
inline bool hitTestBand(const int screenHeight, const int viewTop, const int viewBottom, const int sbTop,
                        const int sbBottom, const bool barVisible, const int y) {
  if (screenHeight <= 0 || y < 0 || y >= screenHeight) return false;
  if (barVisible) {
    if (sbBottom > 0 && y >= screenHeight - viewBottom - std::max(kMinTapBandPx, sbBottom)) return true;
    if (sbTop > 0 && y < viewTop + std::max(kMinTapBandPx, sbTop)) return true;
    return false;
  }
  return y >= screenHeight - viewBottom - kMinTapBandPx;
}

}  // namespace status_bar_tap
