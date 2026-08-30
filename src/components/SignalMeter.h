#pragma once

// The four-bar WiFi strength icon and the cross that replaces it when the link
// is down. Pure geometry so the bars can be measured in a host test and so
// every screen that shows signal puts it in the same place, at the right end of
// the sub-header band.
namespace signal_meter {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

constexpr int kBarCount = 4;
constexpr int kBarWidth = 4;
constexpr int kBarGap = 2;
constexpr int kHeight = 14;

inline int width() { return kBarCount * kBarWidth + (kBarCount - 1) * kBarGap; }

// The icon's own box, pinned to the right edge of the band it sits in and
// standing on the band's baseline.
inline Rect iconRect(const int bandRight, const int bandBottom) {
  return Rect{bandRight - width(), bandBottom - kHeight, width(), kHeight};
}

// Bar i, counting from the left. They climb in even steps, so the shortest is a
// quarter of the icon and the tallest is all of it.
inline Rect barRect(const Rect& icon, const int index) {
  const int height = (index + 1) * kHeight / kBarCount;
  return Rect{icon.x + index * (kBarWidth + kBarGap), icon.y + kHeight - height, kBarWidth, height};
}

// Bars up to this many are filled; the rest are outlines, so the icon always
// shows the full scale and not just the part that is lit.
inline bool barIsFilled(const int index, const int bars) { return index < bars; }

}  // namespace signal_meter
