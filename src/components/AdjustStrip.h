#pragma once

#include <algorithm>

#include "LightPanelGeometry.h"

// The number adjuster Text Settings docks under its live preview: a rule, the setting's
// name with its value, then a light-panel row (track, boxed minus, boxed plus). Pure
// geometry, so the paint and the touch hit test read the same rects and the layout is
// testable on the host.
namespace adjust_strip {

using light_panel::Rect;

constexpr int kRule = 2;
constexpr int kPadY = 6;

struct Layout {
  Rect frame;  // the whole strip, rules included
  Rect label;  // name on the left, value on the right
  Rect row;    // the control row: a touch anywhere left of the minus lands on the track
  Rect track;
  Rect minus;
  Rect plus;
};

inline int heightFor(const int lineHeight) {
  return kRule + kPadY + lineHeight + light_panel::kStepRowHeight + kPadY + kRule;
}

inline Layout forStrip(const int y, const int width, const int lineHeight) {
  using namespace light_panel;
  Layout layout{};
  layout.frame = Rect{0, y, width, heightFor(lineHeight)};
  layout.label = Rect{kSidePad, y + kRule + kPadY, width - kSidePad * 2, lineHeight};
  const int rowY = layout.label.y + lineHeight;
  const int stepY = rowY + (kStepRowHeight - kStepHeight) / 2;
  const int plusX = width - kSidePad - kStepWidth;
  const int minusX = plusX - kStepGap - kStepWidth;
  layout.minus = Rect{minusX, stepY, kStepWidth, kStepHeight};
  layout.plus = Rect{plusX, stepY, kStepWidth, kStepHeight};
  const int trackWidth = std::max(0, minusX - kStepsGap - kSidePad);
  layout.track = Rect{kSidePad, rowY + (kStepRowHeight - kTrackHeight) / 2, trackWidth, kTrackHeight};
  // The track takes the touch over the whole row height and out to the screen edge, so it
  // is a thumb-sized target rather than a 22 px line to aim at.
  layout.row = Rect{0, rowY, minusX - kStepsGap / 2, kStepRowHeight};
  return layout;
}

enum class Hit : unsigned char { None, Strip, Track, Minus, Plus };

inline Hit hitTest(const Layout& layout, const int x, const int y) {
  using light_panel::insideRect;
  if (insideRect(layout.minus, x, y)) return Hit::Minus;
  if (insideRect(layout.plus, x, y)) return Hit::Plus;
  if (insideRect(layout.row, x, y)) return Hit::Track;
  if (insideRect(layout.frame, x, y)) return Hit::Strip;
  return Hit::None;
}

}  // namespace adjust_strip
