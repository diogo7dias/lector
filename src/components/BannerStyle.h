#pragma once

#include "fontIds.h"

// The one black banner look in the firmware.
//
// Two draw paths produce it — the wake/unlock banners (UnlockBanners.cpp) and every
// message popup (BaseTheme::drawBannerStrip) — and they must be indistinguishable, so
// the numbers live here rather than being written out twice.
//
// A banner is a full-width black band that reaches the screen's physical edge, with a
// white rule on the edge that faces the page and white centered text inside. It is
// only as tall as its text plus PAD above and below: sized off the popup margins it
// read as a heavy slab with a white gap above it.
namespace banner {

// UI_10, the size the wake banners use. UI_12 read as bolder and taller than the rest
// of the chrome.
constexpr int FONT_ID = UI_10_FONT_ID;

// Blank above and below the text.
constexpr int PAD = 10;

// White rule on the page-facing edge. The band spans the screen and reaches its
// physical edges, so a full frame would just box in a strip.
constexpr int RULE = 2;

}  // namespace banner
