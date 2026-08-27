#pragma once

#include <algorithm>
#include <cstdint>

// Where a two-column grid of setting cells lands.
//
// The settings screens used to be one column of full-width rows, which put about eight of
// twenty-two settings on screen and left the rest below the fold. A cell carries the same
// two pieces of text a row did — the name and the current value — stacked instead of
// spread, so two fit side by side and the scroll halves.
//
// Pure geometry, so the draw code stays a draw loop and this can be tested on the host
// without a renderer.
namespace settings_grid {

constexpr int kColumns = 2;
constexpr int kSidePad = 12;
constexpr int kGap = 8;
// Two lines of text plus air. Also the smallest cell worth aiming a thumb at.
constexpr int kMinCellHeight = 58;

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

struct Layout {
  int count;
  int cellWidth;
  int cellHeight;
  // Cell rows that fit in the pane. At least one, even when the pane is shorter than a
  // cell: a grid that draws nothing cannot be scrolled back into view.
  int visibleRows;
  int totalRows;
  // First cell row drawn, after clamping the caller's offset to what exists.
  int scrollRow;
};

inline int rowsFor(const int count) { return (count + kColumns - 1) / kColumns; }

// `cellHeight` is grown to fill the pane when the whole grid fits, so a short grid does
// not sit as a band of small boxes with a gap under it.
inline Layout forPane(const int paneWidth, const int paneHeight, const int count, const int scrollRow) {
  Layout layout{};
  layout.count = std::max(0, count);
  layout.cellWidth = (paneWidth - kSidePad * 2 - kGap * (kColumns - 1)) / kColumns;
  layout.totalRows = rowsFor(layout.count);

  const int step = kMinCellHeight + kGap;
  layout.visibleRows = std::max(1, (paneHeight + kGap) / step);
  layout.cellHeight = kMinCellHeight;
  if (layout.totalRows > 0 && layout.totalRows <= layout.visibleRows) {
    // Everything fits: spread the rows over the pane rather than leaving dead space.
    layout.visibleRows = layout.totalRows;
    layout.cellHeight = std::max(kMinCellHeight, (paneHeight - kGap * (layout.totalRows - 1)) / layout.totalRows);
  }

  const int maxScroll = std::max(0, layout.totalRows - layout.visibleRows);
  layout.scrollRow = std::clamp(scrollRow, 0, maxScroll);
  return layout;
}

// The rect for cell `index`, or a zero-width rect when it is scrolled out of the pane.
inline Rect cellAt(const Layout& layout, const int paneTop, const int index) {
  // An odd count leaves the last cell of the last row empty; asking for it is not an
  // error, it simply has no rect.
  if (index < 0 || index >= layout.count) return Rect{};
  const int row = index / kColumns - layout.scrollRow;
  if (row < 0 || row >= layout.visibleRows) return Rect{};
  const int column = index % kColumns;
  return Rect{kSidePad + column * (layout.cellWidth + kGap), paneTop + row * (layout.cellHeight + kGap),
              layout.cellWidth, layout.cellHeight};
}

// The scroll row that brings `index` into view, moved as little as possible.
inline int scrollToShow(const Layout& layout, const int index) {
  const int row = index / kColumns;
  if (row < layout.scrollRow) return row;
  if (row >= layout.scrollRow + layout.visibleRows) return row - layout.visibleRows + 1;
  return layout.scrollRow;
}

// Where a step lands, in cells. Up and Down move a whole row so the column is kept;
// Left and Right move one cell, which is what makes the second column reachable.
// Clamped rather than wrapped: a wrap at the end of a settings screen reads as a jump.
inline int step(const int index, const int count, const int deltaRows, const int deltaCells) {
  if (count <= 0) return 0;
  const int moved = index + deltaRows * kColumns + deltaCells;
  return std::clamp(moved, 0, count - 1);
}

}  // namespace settings_grid
