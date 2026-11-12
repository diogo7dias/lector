#pragma once

#include "fontIds.h"

// The one banner look in the firmware.
//
// Message popups (BaseTheme::drawBannerStrip) and light panels share these metrics.
//
// A banner is a full-width white band that reaches the screen's physical edge, with a
// black rule on the edge that faces the page and black centered text inside. It is
// only as tall as its text plus PAD above and below.
//
// White, not black: the banner shows on every wait and screen change, and a black slab
// FAST-refreshed that often left its ghost behind, most visibly on the lock screen's
// wallpaper, which follows the "Entering sleep" banner. White is also what the light
// panel already uses.
namespace banner {

// UI_10 matches the UI text. UI_12 read as bolder and taller than the rest
// of the chrome.
constexpr int FONT_ID = UI_10_FONT_ID;

// Blank above and below the text. The band is PAD + one line box + PAD, so this is the
// only number that sets its thickness: at UI_10 (line box 25px) the band is 2*PAD+25.
// Kept small on purpose — the banner should read as a rule with a word on it, not a slab.
constexpr int PAD = 6;

// White rule on the page-facing edge. The band spans the screen and reaches its
// physical edges, so a full frame would just box in a strip.
constexpr int RULE = 2;

}  // namespace banner
