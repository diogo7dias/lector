#pragma once

#include <algorithm>

#include "components/themes/BaseTheme.h"

// Finger-sized geometry for boards with a touch panel. The button metrics were drawn for
// a device you only ever press physical keys on, so every interactive band is smaller than
// a fingertip: 30 px list rows, a 40 px hint band. This raises exactly those bands and
// leaves everything else — headers, padding, progress bars — where the look was tuned.
namespace touch_metrics {

// The smallest a target may be. 48 px is the floor both platform guidelines settle on, and
// on the X4 Pro's 480 px-wide panel it is about 5 mm of glass.
constexpr int kMinTarget = 48;

// Chosen above the floor, not at it: a row that measures exactly the minimum leaves no room
// for the miss, and these bands are what a reader hits over and over.
constexpr int kListRowHeight = 56;
constexpr int kListWithSubtitleRowHeight = 72;
constexpr int kMenuRowHeight = 60;
constexpr int kButtonHintsHeight = 56;
constexpr int kKeyboardKeyHeight = 62;

// Raises only the interactive bands, and only upwards: a theme that already asks for a
// roomier row keeps it.
inline ThemeMetrics adjusted(const ThemeMetrics& base) {
  ThemeMetrics touch = base;
  touch.listRowHeight = std::max(base.listRowHeight, kListRowHeight);
  touch.listWithSubtitleRowHeight = std::max(base.listWithSubtitleRowHeight, kListWithSubtitleRowHeight);
  touch.menuRowHeight = std::max(base.menuRowHeight, kMenuRowHeight);
  touch.buttonHintsHeight = std::max(base.buttonHintsHeight, kButtonHintsHeight);
  touch.keyboardKeyHeight = std::max(base.keyboardKeyHeight, kKeyboardKeyHeight);
  return touch;
}

}  // namespace touch_metrics
