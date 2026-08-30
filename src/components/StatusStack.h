#pragma once

// Where the block of lines on a status screen sits. Pure arithmetic, so the
// vertical centring is testable without a panel: every one of these screens
// used to place its text at hand-written offsets from the middle of the screen,
// which is exactly the kind of measurement that goes wrong when the type size
// or the theme changes under it.
namespace status_stack {

struct Metrics {
  int headlineHeight = 0;  // first line, in the body face
  int lineHeight = 0;      // every line after it, in the smaller face
  int gap = 0;             // between lines
  int progressHeight = 0;  // 0 when the screen shows no bar
};

// The block's own height: the lines, the gaps between them, and the bar with a
// double gap above it when there is one.
inline int heightFor(const Metrics& metrics, const int lineCount, const bool showProgress) {
  if (lineCount <= 0 && !showProgress) return 0;
  int height = 0;
  for (int i = 0; i < lineCount; ++i) {
    height += i == 0 ? metrics.headlineHeight : metrics.lineHeight;
    if (i > 0) height += metrics.gap;
  }
  if (showProgress) height += metrics.gap * 2 + metrics.progressHeight;
  return height;
}

// The block's top, centred in the body. A block taller than the body starts at
// the top of it rather than above it, so the headline is never cut off.
inline int topFor(const Metrics& metrics, const int bodyY, const int bodyHeight, const int lineCount,
                  const bool showProgress) {
  const int height = heightFor(metrics, lineCount, showProgress);
  const int top = bodyY + (bodyHeight - height) / 2;
  return top < bodyY ? bodyY : top;
}

}  // namespace status_stack
