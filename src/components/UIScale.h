#pragma once
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight).
//
// One size for every slot. The reference is the bottom button-hint strip, drawn in
// UI_10; every list row, header, value and popup matches it, so the small and
// title slots carry the same face as the body. Nothing in the UI is bigger or
// smaller than a hint label.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

constexpr int UI_FONT_ID = UI_10_FONT_ID;

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  spec.smallFontId = UI_FONT_ID;
  spec.bodyFontId = UI_FONT_ID;
  // The UI font, not a reader font: fui headers draw book and directory titles, and
  // the built-in UI fonts cover Hebrew (plus the size-matched SD CJK fallback) where
  // the NotoSans reader subsets do not.
  spec.titleFontId = UI_FONT_ID;
  return spec;
}
