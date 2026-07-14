#pragma once

#include <cstdint>

// Pure decision logic for windowed list refreshes (no hardware deps — host
// testable). A menu frame that differs from the previous one only by which row
// is selected can be refreshed as a small window (old row ∪ new row) instead
// of a full-panel pass. Everything else — different list geometry, different
// content (extraHash), pagination change, first frame — must refresh the full
// panel, because the panel outside the window keeps showing the old frame.
namespace list_window {

struct FrameSnapshot {
  // List rect in logical (orientation-relative) coordinates.
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int rowHeight = 0;
  int pageItems = 0;
  int itemCount = 0;
  int selectedIndex = -1;
  // Hash of everything OUTSIDE the list that affects pixels (header text,
  // button hints, current folder path...). Callers must fold in anything that
  // can change while the list geometry stays identical.
  uint32_t extraHash = 0;
  // Number of drawList calls in the frame. Windowing is only safe when the
  // frame contains exactly one list.
  int listDrawCalls = 0;
  bool valid = false;
};

struct WindowRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// FNV-1a, for building extraHash from strings.
inline uint32_t hash32(const char* s, uint32_t seed = 2166136261u) {
  uint32_t h = seed;
  while (s != nullptr && *s != '\0') {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}

// Decide whether the prev -> cur transition is a pure same-page selection move
// and, if so, produce the union rect of the old and new selection rows (with
// the 2px overhang the selection fill paints above the row). Returns false for
// anything that is not provably selection-only.
inline bool planSelectionWindow(const FrameSnapshot& prev, const FrameSnapshot& cur, WindowRect* out) {
  if (!prev.valid || !cur.valid) return false;
  if (prev.listDrawCalls != 1 || cur.listDrawCalls != 1) return false;
  if (prev.x != cur.x || prev.y != cur.y || prev.width != cur.width || prev.height != cur.height) return false;
  if (prev.rowHeight != cur.rowHeight || prev.pageItems != cur.pageItems) return false;
  if (prev.itemCount != cur.itemCount) return false;
  if (prev.extraHash != cur.extraHash) return false;
  if (cur.rowHeight <= 0 || cur.pageItems <= 0) return false;
  if (prev.selectedIndex < 0 || cur.selectedIndex < 0) return false;
  if (prev.selectedIndex == cur.selectedIndex) return false;
  if (prev.selectedIndex >= cur.itemCount || cur.selectedIndex >= cur.itemCount) return false;
  // Same page only: crossing a page boundary redraws every row.
  if (prev.selectedIndex / cur.pageItems != cur.selectedIndex / cur.pageItems) return false;

  const int rowA = prev.selectedIndex % cur.pageItems;
  const int rowB = cur.selectedIndex % cur.pageItems;
  const int top = rowA < rowB ? rowA : rowB;
  const int bottom = rowA < rowB ? rowB : rowA;
  // The selection fill starts 2px above the row top (BaseTheme::drawList);
  // extend the window the same amount on both ends.
  out->x = cur.x;
  out->y = cur.y + top * cur.rowHeight - 2;
  out->width = cur.width;
  out->height = (bottom - top + 1) * cur.rowHeight + 4;
  return true;
}

}  // namespace list_window
