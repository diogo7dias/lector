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

// What the block is made of, in drawing order: the lines, then a QR square with
// its own lines under it, then the bar.
struct Content {
  int lineCount = 0;
  int qrSize = 0;       // the square's side; 0 when the screen shows no code
  int qrLineCount = 0;  // lines under the code (the address it encodes)
  bool showProgress = false;
};

// The block's own height: every part, with a gap between lines and a double gap
// before the code and before the bar, which are the two things that need air.
inline int heightFor(const Metrics& metrics, const Content& content) {
  int height = 0;
  for (int i = 0; i < content.lineCount; ++i) {
    height += i == 0 ? metrics.headlineHeight : metrics.lineHeight;
    if (i > 0) height += metrics.gap;
  }
  if (content.qrSize > 0) {
    if (height > 0) height += metrics.gap * 2;
    height += content.qrSize;
  }
  for (int i = 0; i < content.qrLineCount; ++i) {
    height += metrics.gap + metrics.lineHeight;
  }
  if (content.showProgress) height += metrics.gap * 2 + metrics.progressHeight;
  return height;
}

// The block's top, centred in the body. A block taller than the body starts at
// the top of it rather than above it, so the headline is never cut off.
inline int topFor(const Metrics& metrics, const int bodyY, const int bodyHeight, const Content& content) {
  const int height = heightFor(metrics, content);
  const int top = bodyY + (bodyHeight - height) / 2;
  return top < bodyY ? bodyY : top;
}

}  // namespace status_stack
