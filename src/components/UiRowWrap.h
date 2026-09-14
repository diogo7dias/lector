#pragma once

#include <algorithm>
#include <functional>

// How tall a name-and-value row has to be so nothing in it is cut. Pure arithmetic over
// measured widths and wrapped line counts, so the settings rows can be tested on the host.
//
// A row shows a setting's name on the left and its value on the right. When both fit on
// one line that is the row. When they do not, the value drops to its own line under the
// name, right-aligned, and the name wraps over the full width; the row grows by exactly
// the lines the two need. Nothing is ever truncated to keep a fixed height.
namespace ui_row_wrap {

struct Layout {
  bool valueBelow;  // the value sits on its own line(s) under the name
  int nameLines;    // >= 1
  int valueLines;   // 0 without a value
  int height;       // lineHeight * lines + padY
};

// nameLinesFor(width) / valueLinesFor(width) report how many lines the text wraps to in
// that width; they are only called when the text does not fit a single line, so a row
// that fits pays two width measurements and nothing else.
inline Layout forRow(const int nameWidth, const int valueWidth, const int availWidth, const int gap,
                     const int lineHeight, const int padY, const std::function<int(int width)>& nameLinesFor,
                     const std::function<int(int width)>& valueLinesFor) {
  const int width = std::max(1, availWidth);
  const auto lines = [&](const std::function<int(int)>& count, const int textWidth) {
    if (textWidth <= width || !count) return 1;
    return std::max(1, count(width));
  };
  Layout layout{false, 1, 0, 0};
  if (valueWidth <= 0) {
    layout.nameLines = lines(nameLinesFor, nameWidth);
  } else if (nameWidth + gap + valueWidth <= width) {
    layout.valueLines = 1;
  } else {
    layout.valueBelow = true;
    layout.nameLines = lines(nameLinesFor, nameWidth);
    layout.valueLines = lines(valueLinesFor, valueWidth);
  }
  const int total = layout.valueBelow ? layout.nameLines + layout.valueLines : layout.nameLines;
  layout.height = total * lineHeight + padY;
  return layout;
}

// A cell that stacks the name over the value (the two-column touch grid): as tall as
// both wrapped, plus padding.
inline int stackedHeight(const int nameLines, const int valueLines, const int lineHeight, const int padY) {
  return (std::max(1, nameLines) + std::max(0, valueLines)) * lineHeight + padY;
}

}  // namespace ui_row_wrap
