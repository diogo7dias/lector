#pragma once

#include <algorithm>
#include <cstdint>

// The arithmetic behind the reader's toolbar (Settings > Reader Menu Style > Toolbar), kept
// free of the UI so a host test can pin it down.
namespace reader_toolbar {

// The three tiles under the scrub row, in screen order.
enum Tool : uint8_t { Contents = 0, Text = 1, More = 2 };
constexpr int TOOL_COUNT = 3;

// Left or right on the focused tile wraps around the row.
constexpr int stepTool(const int tool, const int direction) {
  return ((tool + direction) % TOOL_COUNT + TOOL_COUNT) % TOOL_COUNT;
}

// A spine item kept inside the book, so a scrub past either end lands on the first or last.
constexpr int clampSpine(const int spine, const int spineCount) {
  return spineCount <= 0 ? 0 : std::clamp(spine, 0, spineCount - 1);
}

// The spine item under a point on the progress track, 0 (start) to 1000 (end), rounded to
// the nearest item.
constexpr int spineForPermille(const int permille, const int spineCount) {
  if (spineCount <= 1) return 0;
  return clampSpine((std::clamp(permille, 0, 1000) * (spineCount - 1) + 500) / 1000, spineCount);
}

}  // namespace reader_toolbar
