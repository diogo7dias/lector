#pragma once
#include <HalGPIO.h>

#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight).
// On keys-only boards (X3, X4), fonts match the 0.27 layout: UI_10 for body/list rows,
// SMALL_FONT_ID for subtitles. On touch boards (X4 Pro), touch-tier fonts are used.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  if (!gpio.hasTouch()) {
    spec.smallFontId = SMALL_FONT_ID;
    spec.bodyFontId = UI_10_FONT_ID;
    spec.titleFontId = UI_10_FONT_ID;
  } else {
    spec.smallFontId = UI_10_FONT_ID;
    spec.bodyFontId = UI_12_FONT_ID;
    // Titles use the UI font, not a reader font: fui headers draw book and
    // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
    // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
    spec.titleFontId = UI_12_FONT_ID;
  }
  return spec;
}
