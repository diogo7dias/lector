#pragma once

// The drag bar drawn inside an armed numeric settings row. Pure geometry so the
// track, the fill and the value a touch maps to are all host-testable, and so the
// draw and the hit test can never disagree: both ask this for the same rect.
namespace row_slider {

struct Bar {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Air between the bar and the row edges. The bar sits inside the row's own band,
// so a drag along it cannot be read as a swipe on the row above or below.
constexpr int kSidePad = 16;
constexpr int kHeight = 8;
// Below this the row has no space for a track worth touching, and the caller
// draws the plain number instead.
constexpr int kMinRowHeight = 14;
constexpr int kMinWidth = 40;

// The track between a row's label and its number, both already measured by the
// caller. Same air on each side as barFor, so an armed row and a full-width bar
// read as the same control.
inline Bar barBetween(const int leftX, const int rightX, const int rowY, const int rowHeight) {
  if (rowHeight < kMinRowHeight) return {};
  const int width = rightX - leftX - kSidePad * 2;
  if (width < kMinWidth) return {};
  return Bar{leftX + kSidePad, rowY + (rowHeight - kHeight) / 2, width, kHeight};
}

inline Bar barFor(const int rowX, const int rowY, const int rowWidth, const int rowHeight) {
  if (rowHeight < kMinRowHeight) return {};
  const int width = rowWidth - kSidePad * 2;
  if (width < kMinWidth) return {};
  return Bar{rowX + kSidePad, rowY + (rowHeight - kHeight) / 2, width, kHeight};
}

// The value a touch at `x` picks, snapped to `step` and clamped to the range. The
// far end of the track always yields maxValue, even when the span is not a whole
// number of steps (line spacing runs 35 to 150, and 150 must stay reachable).
inline int valueForX(const Bar& bar, const int x, const int minValue, const int maxValue, const int step) {
  if (bar.width <= 0 || maxValue <= minValue) return minValue;
  if (x <= bar.x) return minValue;
  if (x >= bar.x + bar.width) return maxValue;

  const int span = maxValue - minValue;
  const int raw = minValue + ((x - bar.x) * span + bar.width / 2) / bar.width;
  if (step <= 1) return raw;
  const int snapped = minValue + ((raw - minValue) + step / 2) / step * step;
  return snapped > maxValue ? maxValue : snapped;
}

// How much of the track is filled for `value`.
inline int filledWidth(const Bar& bar, const int value, const int minValue, const int maxValue) {
  if (bar.width <= 0 || maxValue <= minValue) return 0;
  const int clamped = value < minValue ? minValue : (value > maxValue ? maxValue : value);
  return (clamped - minValue) * bar.width / (maxValue - minValue);
}

}  // namespace row_slider
