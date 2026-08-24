#pragma once

#include <cstdint>

// Which list item a tap landed on. The list draws know exactly where each row went — a
// fixed 56 px step, a wrapped row two lines tall, a menu tile — so they record the rects
// as they paint and the input side reads them back. That keeps one answer for every list
// shape instead of a hit-test that re-derives geometry the draw already worked out.
namespace row_hit {

constexpr int kNoItem = -1;
// A page of rows never exceeds this: 800 px of screen over a 30 px row leaves 26.
constexpr int kMaxRows = 32;

struct Rows {
  struct Entry {
    int item;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
  };

  Entry entries[kMaxRows] = {};
  int count = 0;

  // Called at the start of a list draw: last frame's rows are gone the moment this one
  // starts painting, so a tap can never act on a row that is no longer on screen.
  void begin() { count = 0; }

  void add(const int item, const int x, const int y, const int width, const int height) {
    if (count >= kMaxRows) return;
    entries[count++] = Entry{item, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                             static_cast<int16_t>(height)};
  }

  int itemAt(const int x, const int y) const {
    for (int i = 0; i < count; ++i) {
      const Entry& e = entries[i];
      if (x >= e.x && x < e.x + e.width && y >= e.y && y < e.y + e.height) return e.item;
    }
    return kNoItem;
  }
};

// The rows the most recent list draw painted. Same one-frame contract as
// hint_band::lastPainted(): whatever is on screen right now is what answers to a tap.
inline Rows& lastRows() {
  static Rows rows;
  return rows;
}

}  // namespace row_hit
