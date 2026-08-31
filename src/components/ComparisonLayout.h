#pragma once

// Where the two-sided comparison block on a status screen sits: a headline, one
// labelled block per side (theirs and ours), and a line saying which is ahead.
// Pure arithmetic, so the vertical placement is testable without a panel. Both
// sync screens used to draw this at hand-written offsets (top + 40, + 65, + 90,
// + 150...), which is exactly what breaks when the type size or the theme
// changes under it.
namespace comparison_layout {

// The two sides always take the same shape, so one set of heights covers both.
struct Metrics {
  int headlineHeight = 0;  // the "position found" line, in the body face
  int labelHeight = 0;     // a side's own label, in the body face
  int lineHeight = 0;      // a side's detail lines, in the smaller face
  int gap = 0;             // between lines; doubled between blocks
};

struct Content {
  bool hasHeadline = false;
  int sideLines[2] = {0, 0};  // detail lines under each side's label
  bool hasRelation = false;   // the "further ahead" line under both sides
};

// One side: its label, then a line per detail, with a gap between the lines.
inline int sideHeightFor(const Metrics& metrics, const int lines) {
  int height = metrics.labelHeight;
  for (int i = 0; i < lines; ++i) height += metrics.gap + metrics.lineHeight;
  return height;
}

// The whole block, with a double gap before each side and before the relation:
// those are the seams the eye needs to separate one reader's position from the
// other's.
inline int heightFor(const Metrics& metrics, const Content& content) {
  int height = content.hasHeadline ? metrics.headlineHeight : 0;
  for (int side = 0; side < 2; ++side) {
    if (height > 0) height += metrics.gap * 2;
    height += sideHeightFor(metrics, content.sideLines[side]);
  }
  if (content.hasRelation) height += metrics.gap * 2 + metrics.lineHeight;
  return height;
}

// The block's top, centred in whatever band is left above the choices. A block
// taller than the band starts at the top of it rather than above it, so the
// headline is never cut off.
inline int topFor(const Metrics& metrics, const int bandY, const int bandHeight, const Content& content) {
  const int height = heightFor(metrics, content);
  const int top = bandY + (bandHeight - height) / 2;
  return top < bandY ? bandY : top;
}

}  // namespace comparison_layout
