#pragma once

#include <algorithm>
#include <cstdint>

// The arithmetic behind the reader's toolbar (Settings > Reader Menu Style > Toolbar), kept
// free of the UI so a host test can pin it down.
namespace reader_toolbar {

// The four tiles under the scrub row, in screen order.
enum Tool : uint8_t { Contents = 0, Bookmark = 1, Text = 2, More = 3 };
constexpr int TOOL_COUNT = 4;

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

// Single-spine or two-spine books (short stories, essays, cover+body) have almost nothing to
// scrub by spine; scrub pages inside the open chapter instead.
constexpr bool scrubsPages(const int spineCount) { return spineCount <= 2; }

constexpr int clampPage(const int page, const int pageCount) {
  return pageCount <= 0 ? 0 : std::clamp(page, 0, pageCount - 1);
}

constexpr int pageForPermille(const int permille, const int pageCount) {
  if (pageCount <= 1) return 0;
  return clampPage((std::clamp(permille, 0, 1000) * (pageCount - 1) + 500) / 1000, pageCount);
}

constexpr int permilleForPage(const int page, const int pageCount) {
  if (pageCount <= 1) return 0;
  return (clampPage(page, pageCount) * 1000 + (pageCount - 1) / 2) / (pageCount - 1);
}

}  // namespace reader_toolbar
