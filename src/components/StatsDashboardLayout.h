#pragma once

#include <algorithm>

// Where the blocks of the reading-stats screen sit: the metric grid, the rows
// of the time-of-day chart and the columns of the weekday chart. Pure geometry,
// so the cells can be measured in a host test and the last column always eats
// the rounding rather than leaving a gap at the right edge.
namespace stats_dashboard {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// One cell of the metric grid, in reading order. The cells in the last column
// and the last row take whatever the division left over, so the grid meets its
// own edges exactly.
inline Rect metricCell(const Rect& area, const int index, const int columns, const int rows) {
  if (columns <= 0 || rows <= 0) return Rect{};
  const int column = index % columns;
  const int row = index / columns;
  const int cellWidth = area.width / columns;
  const int cellHeight = area.height / rows;
  return Rect{area.x + column * cellWidth, area.y + row * cellHeight,
              column == columns - 1 ? area.width - column * cellWidth : cellWidth,
              row == rows - 1 ? area.height - row * cellHeight : cellHeight};
}

// A horizontal bar chart's row: the label sits in the left column, the track
// fills the rest.
struct BarRow {
  Rect label;
  Rect track;
};

inline BarRow barRow(const Rect& area, const int index, const int count, const int labelWidth,
                     const int rowHeight) {
  const Rect label{area.x, area.y + index * rowHeight, labelWidth, rowHeight};
  const Rect track{area.x + labelWidth, area.y + index * rowHeight, area.width - labelWidth, rowHeight};
  (void)count;
  return BarRow{label, track};
}

inline int barRowHeight(const int areaHeight, const int count, const int minimum = 8) {
  if (count <= 0) return minimum;
  return std::max(minimum, areaHeight / count);
}

// A vertical bar chart's column: the bar is centred in its slot at half the
// slot's width, which leaves the same air on both sides of every bar.
inline Rect chartColumn(const Rect& area, const int index, const int count, const int chartHeight,
                        const int minBarWidth = 3) {
  if (count <= 0) return Rect{};
  const int slotWidth = area.width / count;
  const int barWidth = std::max(minBarWidth, slotWidth / 2);
  return Rect{area.x + index * slotWidth + (slotWidth - barWidth) / 2, area.y, barWidth, chartHeight};
}

// The slot a column's label is centred in, which is wider than the bar itself.
inline Rect chartColumnSlot(const Rect& area, const int index, const int count) {
  if (count <= 0) return Rect{};
  const int slotWidth = area.width / count;
  return Rect{area.x + index * slotWidth, area.y, slotWidth, area.height};
}

// How much of a track or column a value fills. A nonzero value always shows at
// least one pixel: a day with ten minutes on it should not read as a day with
// none.
inline int fillFor(const int full, const uint32_t value, const uint32_t maxValue) {
  if (maxValue == 0 || value == 0) return 0;
  const int fill = static_cast<int>(static_cast<uint64_t>(full) * value / maxValue);
  return std::max(1, fill);
}

}  // namespace stats_dashboard
