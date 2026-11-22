#pragma once

#include "SignalMeter.h"

// Marks drawn at the right end of a list row instead of spelt in its value text: the
// Wi-Fi picker's "saved" chip, padlock and signal bars. Pure geometry, like
// SignalMeter.h, so the slot can be measured in a host test and the paint is nothing
// but filled rectangles and one short label.
namespace row_marks {

using Rect = signal_meter::Rect;

struct Marks {
  int bars = -1;  // 0-4 signal bars; -1 draws none
  bool locked = false;
  bool saved = false;
};

constexpr int kGap = 6;
// The padlock: a 10 x 8 body under a 2 px shackle 6 px tall, 14 px in all, the bars' height.
constexpr int kLockWidth = 10;
constexpr int kLockBodyHeight = 8;
constexpr int kStroke = 2;
constexpr int kChipPadX = 4;

// The slot every row of a marked list reserves, whatever it shows, so the names line
// up and a row with fewer marks leaves its places blank rather than shifting them.
inline int slotWidth(const int chipTextWidth) {
  return chipTextWidth + 2 * kChipPadX + kGap + kLockWidth + kGap + signal_meter::width();
}

// The bars stand at the right edge of the slot, centred on the row.
inline Rect barsRect(const int slotRight, const int centreY) {
  return Rect{slotRight - signal_meter::width(), centreY - signal_meter::kHeight / 2, signal_meter::width(),
              signal_meter::kHeight};
}

inline Rect lockRect(const int slotRight, const int centreY) {
  return Rect{slotRight - signal_meter::width() - kGap - kLockWidth, centreY - signal_meter::kHeight / 2, kLockWidth,
              signal_meter::kHeight};
}

// The padlock in three fills and a body: left post, right post, the bar across their top.
// Index 0-2 is the shackle, 3 the body.
constexpr int kLockParts = 4;
inline Rect lockPart(const Rect& lock, const int index) {
  const int shackleHeight = lock.height - kLockBodyHeight;
  const int inset = 2;
  switch (index) {
    case 0:
      return Rect{lock.x + inset, lock.y, kStroke, shackleHeight};
    case 1:
      return Rect{lock.x + lock.width - inset - kStroke, lock.y, kStroke, shackleHeight};
    case 2:
      return Rect{lock.x + inset, lock.y, lock.width - 2 * inset, kStroke};
    default:
      return Rect{lock.x, lock.y + shackleHeight, lock.width, kLockBodyHeight};
  }
}

// The chip: an outlined box hugging its label, left of the padlock.
inline Rect chipRect(const int slotRight, const int centreY, const int chipTextWidth, const int lineHeight) {
  const int width = chipTextWidth + 2 * kChipPadX;
  const int height = lineHeight + 2;
  return Rect{lockRect(slotRight, centreY).x - kGap - width, centreY - height / 2, width, height};
}

}  // namespace row_marks
