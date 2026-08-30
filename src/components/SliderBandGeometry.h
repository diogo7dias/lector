#pragma once

#include <algorithm>

// The band that replaces a screen's header while a number is being set: a minus button,
// a plus button, the setting's name and value, and a track wide enough to aim at with a
// thumb. Pure geometry, so the draw and the hit test ask the same source and a host test
// can check both without a screen.
namespace slider_band {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct Layout {
  Rect band;   // the whole strip, the header's own rect
  Rect minus;  // square button at the left end
  Rect plus;   // square button at the right end
  Rect text;   // the line holding the name (left) and the value (right)
  Rect track;  // the bar itself
  Rect touch;  // the track's hit region: full height under the text, edge to edge
  bool valid = false;
};

// Air around the buttons and between them and the text column.
constexpr int kPad = 6;
constexpr int kTrackHeight = 10;
// Under this the band cannot hold a text line and a track worth touching, and the caller
// keeps the plain header instead.
constexpr int kMinHeight = 28;
constexpr int kMinTrackWidth = 60;

inline Layout forBand(const int bandX, const int bandY, const int bandWidth, const int bandHeight,
                      const int textLineHeight) {
  Layout layout;
  layout.band = Rect{bandX, bandY, bandWidth, bandHeight};
  if (bandHeight < kMinHeight) return layout;
  const int buttonSide = bandHeight - kPad * 2;
  layout.minus = Rect{bandX + kPad, bandY + kPad, buttonSide, buttonSide};
  layout.plus = Rect{bandX + bandWidth - kPad - buttonSide, bandY + kPad, buttonSide, buttonSide};

  const int innerX = layout.minus.x + layout.minus.width + kPad;
  const int innerWidth = layout.plus.x - kPad - innerX;
  if (innerWidth < kMinTrackWidth) return layout;

  // The text sits on top and the track under it, both inside the band: nothing below the
  // band moves when a value arms, which is the whole point of replacing the header.
  const int textHeight = std::min(textLineHeight, bandHeight - kTrackHeight - kPad);
  layout.text = Rect{innerX, bandY + kPad / 2, innerWidth, textHeight};
  layout.track = Rect{innerX, layout.text.y + textHeight + kPad / 2, innerWidth, kTrackHeight};
  // Aiming at a 10 px bar with a thumb is hopeless, so the whole strip under the text
  // answers for it, out to the buttons on each side.
  layout.touch =
      Rect{innerX, layout.text.y + textHeight, innerWidth, bandY + bandHeight - (layout.text.y + textHeight)};
  layout.valid = true;
  return layout;
}

// The strip a screen's header actually paints. It starts at the top padding, not at the
// panel's first row: on a device whose glass sits behind a bezel those rows are covered,
// and a band drawn from 0 puts its own title under the frame. Screens hand this to
// SliderBand so an armed value covers the title exactly, with nothing below it moving.
inline Rect headerBandRect(const int screenWidth, const int topPadding, const int headerHeight) {
  return Rect{0, topPadding, screenWidth, headerHeight};
}

inline bool contains(const Rect& rect, const int x, const int y) {
  return rect.width > 0 && rect.height > 0 && x >= rect.x && x < rect.x + rect.width && y >= rect.y &&
         y < rect.y + rect.height;
}

enum class Hit { None, Minus, Plus, Track };

inline Hit hitTest(const Layout& layout, const int x, const int y) {
  if (!layout.valid) return Hit::None;
  if (contains(layout.minus, x, y)) return Hit::Minus;
  if (contains(layout.plus, x, y)) return Hit::Plus;
  if (contains(layout.touch, x, y)) return Hit::Track;
  return Hit::None;
}

// Maps a touch x onto the range, snapped to the setting's own step so a drag can only
// produce values the buttons could also reach. Touches short of the track or past it
// clamp to the ends, which is what a thumb sliding off the edge means.
inline int valueForX(const Layout& layout, const int x, const int minValue, const int maxValue, const int step) {
  if (!layout.valid || maxValue <= minValue) return minValue;
  const int usable = std::max(1, layout.track.width - 1);
  const int offset = std::clamp(x - layout.track.x, 0, usable);
  const int range = maxValue - minValue;
  const int raw = minValue + (offset * range + usable / 2) / usable;
  const int snapStep = std::max(1, step);
  const int snapped = minValue + ((raw - minValue + snapStep / 2) / snapStep) * snapStep;
  return std::clamp(snapped, minValue, maxValue);
}

// How much of the track is behind the value. Zero at the low end, the full track at the
// high end, so the bar reads as a fill and not as a knob that never reaches the corner.
inline int fillWidthFor(const Layout& layout, const int value, const int minValue, const int maxValue) {
  if (!layout.valid || maxValue <= minValue) return 0;
  const int clamped = std::clamp(value, minValue, maxValue);
  return layout.track.width * (clamped - minValue) / (maxValue - minValue);
}

}  // namespace slider_band
