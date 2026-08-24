#pragma once

#include <algorithm>

// One list-row height for every FreeInkUI screen.
//
// The SDK derives its row from the body line height as lineHeight * 2 + 8: room
// for a label, a subtitle under it, and air. Lector's lists are single-line
// almost everywhere, so that row reads as a third of the screen given away and
// does not match the rows the legacy screens draw. Scaled once here, at the
// theme token, so every hosted list moves together instead of each screen
// carrying its own number.
namespace ui_row_height {

// Of the SDK's derived height. Picked on the X4 Pro: 58 px down to 40.
constexpr int kPercent = 70;
// A row must still hold its own text. Below this much clearance over the line
// height the label starts touching the row edges.
constexpr int kMinPadding = 4;

// themeRowHeight <= 0 is the SDK's "inherit" sentinel and is passed through
// untouched. lineHeight <= 0 (no font bound yet) simply skips the floor.
inline int scaled(const int themeRowHeight, const int lineHeight) {
  if (themeRowHeight <= 0) return themeRowHeight;
  const int shrunk = themeRowHeight * kPercent / 100;
  if (lineHeight <= 0) return shrunk;
  return std::max(shrunk, lineHeight + kMinPadding);
}

}  // namespace ui_row_height
