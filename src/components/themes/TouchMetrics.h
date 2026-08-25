#pragma once

#include <algorithm>

#include "components/themes/BaseTheme.h"

// Finger-sized geometry for boards with a touch panel. The button metrics were drawn for
// a device you only ever press physical keys on, so the bands you press over and over are
// smaller than a fingertip. This raises exactly those and leaves everything else where the
// look was tuned.
//
// LIST ROWS ARE DELIBERATELY NOT RAISED. Finger-height rows were tried on the X4 Pro and
// cost too much: a third fewer rows per screen, and lists that no longer matched the home
// screen's own rows. A row spans the full width of the screen, so it is a wide target even
// at 30 px, and a miss lands on the neighbouring row rather than on nothing.
namespace touch_metrics {

// The smallest a target may be. 48 px is the floor both platform guidelines settle on, and
// on the X4 Pro's 480 px-wide panel it is about 5 mm of glass.
constexpr int kMinTarget = 48;

// Chosen above the floor, not at it: a band that measures exactly the minimum leaves no
// room for the miss, and a key is only a tenth of the width.
constexpr int kKeyboardKeyHeight = 62;

// THE BUTTON HINT BAND IS DELIBERATELY NOT RAISED EITHER. A 56 px band was tried on the
// X4 Pro and read as a wall across the bottom of every screen; it keeps the button-era
// 40 px instead. A hint slot is a quarter of the screen wide, so it stays an easy target,
// and the pixels go back to the list above it.
//
// Raises only the keyboard, and only upwards: a theme that already asks for a roomier
// key keeps it.
inline ThemeMetrics adjusted(const ThemeMetrics& base) {
  ThemeMetrics touch = base;
  touch.keyboardKeyHeight = std::max(base.keyboardKeyHeight, kKeyboardKeyHeight);
  return touch;
}

}  // namespace touch_metrics
