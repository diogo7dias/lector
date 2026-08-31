#pragma once

#include <algorithm>
#include <cstdint>

// Where a grid of setting cells lands.
//
// The settings screens used to be one column of full-width rows, which put about eight of
// twenty-two settings on screen and left the rest below the fold. A cell carries the same
// two pieces of text a row did — the name and the current value — stacked instead of
// spread, so two fit side by side and the scroll halves.
//
// Only the X4 Pro has a touch panel, and a cell is a thumb target. The keys-only boards
// walk their settings with four buttons, where a second column buys nothing and costs a
// press per move, so they pass a one-column Shape and get the full-width rows they had
// through lector-0.28.0 back.
//
// Pure geometry, so the draw code stays a draw loop and this can be tested on the host
// without a renderer.
namespace settings_grid {

constexpr int kColumns = 2;
constexpr int kSidePad = 12;
constexpr int kGap = 8;
// Two lines of text plus air. Also the smallest cell worth aiming a thumb at.
constexpr int kMinCellHeight = 58;

// What the caller wants laid out. The defaults are the two-column touch grid.
struct Shape {
  int columns = kColumns;
  int sidePad = kSidePad;
  int gap = kGap;
  int minCellHeight = kMinCellHeight;
  // Grow the cells to fill a pane the whole grid already fits in, so a short grid does
  // not sit as a band of small boxes with dead space under it. A list of fixed-height
  // rows does not want this: it wants its own row height and the space left over.
  bool stretchToFill = true;
};

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

struct Layout {
  int count;
  // Carried so cellAt, scrollToShow and step do not need the Shape again.
  int columns;
  int sidePad;
  int gap;
  int cellWidth;
  int cellHeight;
  // Cell rows that fit in the pane. At least one, even when the pane is shorter than a
  // cell: a grid that draws nothing cannot be scrolled back into view.
  int visibleRows;
  int totalRows;
  // First cell row drawn, after clamping the caller's offset to what exists.
  int scrollRow;
};

inline int rowsFor(const int count, const int columns = kColumns) {
  const int cols = columns > 0 ? columns : 1;
  return (count + cols - 1) / cols;
}

inline Layout forPane(const int paneWidth, const int paneHeight, const int count, const int scrollRow,
                      const Shape& shape) {
  Layout layout{};
  layout.count = std::max(0, count);
  layout.columns = shape.columns > 0 ? shape.columns : 1;
  layout.sidePad = shape.sidePad;
  layout.gap = shape.gap;
  layout.cellWidth =
      (paneWidth - shape.sidePad * 2 - shape.gap * (layout.columns - 1)) / layout.columns;
  layout.totalRows = rowsFor(layout.count, layout.columns);

  const int minHeight = shape.minCellHeight > 1 ? shape.minCellHeight : 1;
  const int step = minHeight + shape.gap;
  layout.visibleRows = std::max(1, (paneHeight + shape.gap) / step);
  layout.cellHeight = minHeight;
  if (layout.totalRows > 0 && layout.totalRows <= layout.visibleRows) {
    layout.visibleRows = layout.totalRows;
    // Everything fits: a grid spreads its rows over the pane, a list keeps its row
    // height and leaves the space under the last row.
    if (shape.stretchToFill) {
      layout.cellHeight =
          std::max(minHeight, (paneHeight - shape.gap * (layout.totalRows - 1)) / layout.totalRows);
    }
  }

  const int maxScroll = std::max(0, layout.totalRows - layout.visibleRows);
  layout.scrollRow = std::clamp(scrollRow, 0, maxScroll);
  return layout;
}

inline Layout forPane(const int paneWidth, const int paneHeight, const int count, const int scrollRow) {
  return forPane(paneWidth, paneHeight, count, scrollRow, Shape{});
}

// The rect for cell `index`, or a zero-width rect when it is scrolled out of the pane.
inline Rect cellAt(const Layout& layout, const int paneTop, const int index) {
  // An odd count leaves the last cell of the last row empty; asking for it is not an
  // error, it simply has no rect.
  if (index < 0 || index >= layout.count) return Rect{};
  const int row = index / layout.columns - layout.scrollRow;
  if (row < 0 || row >= layout.visibleRows) return Rect{};
  const int column = index % layout.columns;
  return Rect{layout.sidePad + column * (layout.cellWidth + layout.gap),
              paneTop + row * (layout.cellHeight + layout.gap), layout.cellWidth, layout.cellHeight};
}

// The scroll row that brings `index` into view, moved as little as possible.
inline int scrollToShow(const Layout& layout, const int index) {
  const int row = index / layout.columns;
  if (row < layout.scrollRow) return row;
  if (row >= layout.scrollRow + layout.visibleRows) return row - layout.visibleRows + 1;
  return layout.scrollRow;
}

// Where a step lands, in cells. Up and Down move a whole row so the column is kept;
// Left and Right move one cell, which is what makes the second column reachable.
// Clamped rather than wrapped: a wrap at the end of a settings screen reads as a jump.
inline int step(const int index, const int count, const int deltaRows, const int deltaCells,
                const int columns = kColumns) {
  if (count <= 0) return 0;
  const int moved = index + deltaRows * (columns > 0 ? columns : 1) + deltaCells;
  return std::clamp(moved, 0, count - 1);
}

}  // namespace settings_grid
