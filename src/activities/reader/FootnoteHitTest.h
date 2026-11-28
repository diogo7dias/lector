#pragma once

#include <climits>
#include <cstdint>

namespace footnote_hit {

struct HitBox {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;
  uint8_t footnoteIndex = 0;
};

// Default finger-friendly padding (px) around footnote link word boxes so tiny
// superscript markers like "[1]" or "1" are easy to tap on touch hardware.
constexpr int DEFAULT_TAP_PAD_PX = 12;

// Returns the `footnoteIndex` of the hit box matching `(tapX, tapY)` (expanded
// by `padPx` on every side), choosing the box whose center is closest on overlap.
// Returns -1 when no box matches.
inline int hitTest(const HitBox* boxes, const int count, const int tapX, const int tapY,
                   const int padPx = DEFAULT_TAP_PAD_PX) {
  if (!boxes || count <= 0) return -1;
  int bestIndex = -1;
  int64_t bestDistSq = INT64_MAX;
  for (int i = 0; i < count; ++i) {
    const HitBox& b = boxes[i];
    if (b.width <= 0 || b.height <= 0) continue;
    const int left = static_cast<int>(b.x) - padPx;
    const int top = static_cast<int>(b.y) - padPx;
    const int right = static_cast<int>(b.x) + b.width + padPx;
    const int bottom = static_cast<int>(b.y) + b.height + padPx;
    if (tapX < left || tapX >= right || tapY < top || tapY >= bottom) continue;
    const int64_t dx = static_cast<int64_t>(tapX) * 2 - (static_cast<int64_t>(b.x) * 2 + b.width);
    const int64_t dy = static_cast<int64_t>(tapY) * 2 - (static_cast<int64_t>(b.y) * 2 + b.height);
    const int64_t distSq = dx * dx + dy * dy;
    if (distSq < bestDistSq) {
      bestDistSq = distSq;
      bestIndex = b.footnoteIndex;
    }
  }
  return bestIndex;
}

}  // namespace footnote_hit
