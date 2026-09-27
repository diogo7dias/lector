#pragma once

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"

namespace ReaderUtils {

// Reader text margins from the per-book prefs: oriented viewable insets plus the user
// screen margins. screenMargin is the horizontal margin; the vertical ones are always the
// two stored fields, kept equal by the settings screen while Link Top/Bottom is on.
// Dynamic Margins auto-widens the horizontal margins toward ~62 characters per line, using
// the reader font's average glyph width as the yardstick, floored at 10px (mode 1) or 20px
// (mode 2) and capped at 55px so a narrow orientation keeps a usable viewport. `bottom` is
// the base reading margin only; the reader adds any status-bar band on top. The Text
// settings preview calls this too, so its sample sits where the page will.
inline void readerMargins(const GfxRenderer& renderer, const ReaderPrefs& prefs, int& top, int& right, int& bottom,
                          int& left) {
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  top += prefs.screenMarginTop;
  bottom += prefs.screenMarginBottom;
  int horizontal = prefs.screenMargin;
  if (prefs.dynamicMargins) {
    const int sampleWidth = renderer.getTextWidth(SETTINGS.getReaderFontId(prefs), "abcdefghijklmnopqrstuvwxyz");
    const int avgCharWidth = (sampleWidth > 0) ? sampleWidth / 26 : 8;
    const int targetTextWidth = 62 * avgCharWidth;
    const int availableWidth = renderer.getScreenWidth() - left - right;
    const int minDynamicMargin = (prefs.dynamicMargins >= 2) ? 20 : 10;
    horizontal = std::max(minDynamicMargin, std::min(55, (availableWidth - targetTextWidth) / 2));
  }
  left += horizontal;
  right += horizontal;
}

}  // namespace ReaderUtils
