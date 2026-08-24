#pragma once

#include <cstdint>

// Geometry for the focused-row highlight. Every style is expressed as a short list
// of solid black rectangles so the device side stays a single fillRect loop and the
// shapes themselves can be tested on the host.
namespace selection_style {

enum Style : uint8_t {
  SOLID = 0,  // the whole row filled, text knocked out white (the original look)
  TIGHT = 1,  // a block around the row's own text line, paper left at both ends
  STYLE_COUNT = 2,
};

struct Bar {
  int x;
  int y;
  int width;
  int height;
};

// One rectangle is all any style needs.
constexpr int MAX_BARS = 1;

// Paper left at each end of a tight block, and the air above and below its text
// line. Taken from the home screen's recent-book rows, which have always drawn
// their selection this way.
constexpr int TIGHT_SIDE_INSET = 10;
constexpr int TIGHT_LINE_PAD = 3;

// Reads a persisted setting value. settings.json is user-editable and survives
// downgrades, so anything the build does not know about falls back to the original
// solid highlight rather than indexing past the enum. 0.27 retired brackets (1) and
// the caret (2): slot 1 is the tight block now, and a file still holding 2 lands on
// solid.
constexpr Style fromSetting(const uint8_t value) { return value < STYLE_COUNT ? static_cast<Style>(value) : SOLID; }

// True when the style paints over the row itself, so the row's own text has to be
// drawn white to stay legible. Both styles do; the branch is kept so callers read
// as asking rather than assuming.
constexpr bool invertsText(const Style style) { return style == SOLID || style == TIGHT; }

namespace detail {

constexpr int clampInt(const int value, const int low, const int high) {
  return value < low ? low : (value > high ? high : value);
}

}  // namespace detail

// Writes the bars painting `style` over the given row and returns how many were
// written. A row with no area paints nothing.
//
// firstLineY/firstLineHeight describe the row's FIRST line of text. The tight block
// hugs that line rather than the row's full height, so a row that wraps onto two
// lines is marked where reading starts instead of swallowing the gap under it. Left
// at their defaults the row itself is used, which is right for a single-line row.
inline int bars(const Style style, const int x, const int y, const int width, const int height, Bar out[MAX_BARS],
                const int firstLineY = -1, const int firstLineHeight = 0) {
  if (width <= 0 || height <= 0) return 0;

  if (style == SOLID) {
    out[0] = Bar{x, y, width, height};
    return 1;
  }

  // TIGHT. Trimming both ends off a row narrower than twice the inset would leave
  // nothing to see, so the trim is capped at a third of the row from each side.
  const int inset = detail::clampInt(TIGHT_SIDE_INSET, 0, width / 3);
  const bool haveLine = firstLineY >= 0 && firstLineHeight > 0;
  const int lineTop = haveLine ? firstLineY - TIGHT_LINE_PAD : y;
  const int lineBottom = haveLine ? firstLineY + firstLineHeight + TIGHT_LINE_PAD : y + height;
  const int top = detail::clampInt(lineTop, y, y + height);
  const int bottom = detail::clampInt(lineBottom, top, y + height);

  out[0] = Bar{x + inset, top, width - inset * 2, bottom - top};
  return 1;
}

}  // namespace selection_style
