#pragma once

#include <FreeInkApp.h>

#include <algorithm>

// The text rules UiGridActivity's rows, cells, headings and cards share
// (UiGridActivity.cpp, UiGridCellLook.cpp).
namespace ui_grid_text {

namespace fui = freeink::ui;

// Rows and cells measure through the same FreeInkUI target they are drawn with, so
// the line count the height is built from is the line count text() will draw.
inline int wrappedLines(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, const int width) {
  if (text == nullptr || text[0] == '\0' || width <= 0) return 1;
  const int16_t lineHeight = target.lineHeight(style.font);
  if (lineHeight <= 0) return 1;
  const fui::Size size = fui::measureWrappedText(target, text, style, static_cast<int16_t>(width));
  return std::max(1, size.height / lineHeight);
}

// The label style every row and cell measures and draws with: the body face, wrapping
// over as many lines as it needs.
inline fui::TextStyle wrappingStyle(const fui::ThemeTokens& theme) {
  fui::TextStyle style = theme.bodyText;
  style.maxLines = 8;
  return style;
}

// True when a selected control is drawn with its selected (black) ground. An armed
// touch (Active) and the tap flash (Focused) outrank it with light grounds, so
// inverted text on top of either would vanish.
inline bool paintsSelected(const fui::State state) {
  return !fui::hasState(state, fui::StateActive) && !fui::hasState(state, fui::StateFocused);
}

}  // namespace ui_grid_text
