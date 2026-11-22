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

// Where a marked list's rows went: the arithmetic fui::list() does for one-line rows
// (inset band, the scroll track cut from the rows when the list overflows, a fixed
// stride), so the marks land in the slot the list held back. `visible` is
// fui::listVisibleRows() for the band. Pinned against the real list in RowMarksTest.
struct ListLayout {
  Rect band;
  int count = 0;
  int visible = 0;
  int topIndex = 0;
  int rowHeight = 0;
  int rowGap = 0;
  int rowInset = 0;
  int sidePadding = 0;
  int scrollWidth = 0;
  int scrollInset = 0;
  bool scrollLeft = false;
};

struct RowPlacement {
  int top = 0;  // first row drawn
  int end = 0;  // one past the last
  int slotRight = 0;
  int firstY = 0;
  int stride = 0;
  int rowY(const int index) const { return firstY + (index - top) * stride; }
};

inline RowPlacement placeRows(const ListLayout& l) {
  RowPlacement p;
  const bool overflows = l.count > l.visible;
  p.top = overflows ? (l.topIndex < l.count - 1 ? l.topIndex : l.count - 1) : 0;
  if (p.top < 0) p.top = 0;
  p.end = overflows ? p.top + l.visible : l.count;
  if (p.end > l.count) p.end = l.count;
  int rowX = l.band.x + l.rowInset;
  int rowWidth = l.band.width - 2 * l.rowInset;
  const int needed = l.scrollWidth + l.scrollInset + 2;
  if (overflows && l.scrollWidth > 0 && l.rowInset < needed) {
    const int cut = needed - l.rowInset;
    rowWidth -= cut;
    if (l.scrollLeft) rowX += cut;
  }
  p.slotRight = rowX + rowWidth - l.sidePadding;
  p.firstY = l.band.y;
  p.stride = l.rowHeight + l.rowGap;
  return p;
}

}  // namespace row_marks
