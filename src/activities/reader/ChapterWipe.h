#pragma once

#include <algorithm>

// Chapter Wipe: one crosshatch frame shown before the first page of a chapter entered.
// The hatch is 45-degree lines both ways; drawPixel logs every off-screen pixel, so each
// line is clipped to the screen here first. Pure, so test/chapter_wipe pins it on the host.
namespace chapter_wipe {

// Line spacing and stroke in pixels: about a third of the screen inked, dark enough to
// read as a woodcut, light enough that one FAST pass clears it.
constexpr int SPACING = 12;
constexpr int STROKE = 2;

// The on-screen part of the line x + y == c (falling) or x - y == c (rising) on a w x h
// screen. False when the line misses the screen.
inline bool diagonal(const int c, const bool rising, const int w, const int h, int& x1, int& y1, int& x2, int& y2) {
  if (rising) {
    x1 = std::max(0, c);
    x2 = std::min(w - 1, c + h - 1);
    if (x1 > x2) return false;
    y1 = x1 - c;
    y2 = x2 - c;
  } else {
    x1 = std::max(0, c - (h - 1));
    x2 = std::min(w - 1, c);
    if (x1 > x2) return false;
    y1 = c - x1;
    y2 = c - x2;
  }
  return true;
}

// Calls line(x1, y1, x2, y2) for every one-pixel diagonal of the hatch on a w x h screen;
// a stroke is STROKE neighbouring diagonals.
template <class F>
void forEachHatchLine(const int w, const int h, F&& line) {
  int x1, y1, x2, y2;
  for (int c = 0; c <= w + h - 2; c += SPACING) {
    for (int s = 0; s < STROKE; ++s) {
      if (diagonal(c + s, false, w, h, x1, y1, x2, y2)) line(x1, y1, x2, y2);
    }
  }
  for (int c = -(h - 1); c <= w - 1; c += SPACING) {
    for (int s = 0; s < STROKE; ++s) {
      if (diagonal(c + s, true, w, h, x1, y1, x2, y2)) line(x1, y1, x2, y2);
    }
  }
}

}  // namespace chapter_wipe
