#pragma once

#include <cstdint>

// Geometry for the focused-row highlight. Every style is expressed as a short list
// of solid black rectangles so the device side stays a single fillRect loop and the
// shapes themselves can be tested on the host.
namespace selection_style {

enum Style : uint8_t {
  SOLID = 0,     // the whole row filled, text knocked out white (the original look)
  BRACKETS = 1,  // an L at each corner, text left black
  CARET = 2,     // an arrow at the left plus a rule under the row, text left black
  STYLE_COUNT = 3,
};

struct Bar {
  int x;
  int y;
  int width;
  int height;
};

// Eight arms is the largest any style needs.
constexpr int MAX_BARS = 8;

// Reads a persisted setting value. settings.json is user-editable and survives
// downgrades, so anything the build does not know about falls back to the original
// solid highlight rather than indexing past the enum.
constexpr Style fromSetting(const uint8_t value) { return value < STYLE_COUNT ? static_cast<Style>(value) : SOLID; }

// True when the style paints over the row itself, so the row's own text has to be
// drawn white to stay legible. The other styles leave the paper alone.
constexpr bool invertsText(const Style style) { return style == SOLID; }

// Grows a text span so bracket marks sit just outside the glyphs rather than on
// them, clamped so they can never reach into the rows above or below. A span with
// no width is left alone: a row with no value text must not sprout stray brackets.
Bar inflatedSpan(Bar span, Bar row);

namespace detail {

constexpr int clampInt(const int value, const int low, const int high) {
  return value < low ? low : (value > high ? high : value);
}

// Stroke width, thinned on rows too short or too narrow to carry the full 3px.
constexpr int strokeFor(const int width, const int height) {
  const int limit = (width < height ? width : height) / 2;
  return clampInt(3, 1, limit < 1 ? 1 : limit);
}

}  // namespace detail

inline Bar inflatedSpan(const Bar span, const Bar row) {
  if (span.width <= 0 || span.height <= 0) return span;

  constexpr int padX = 5;
  constexpr int padY = 3;
  const int left = detail::clampInt(span.x - padX, row.x, row.x + row.width);
  const int top = detail::clampInt(span.y - padY, row.y, row.y + row.height);
  const int right = detail::clampInt(span.x + span.width + padX, row.x, row.x + row.width);
  const int bottom = detail::clampInt(span.y + span.height + padY, row.y, row.y + row.height);
  return Bar{left, top, right - left, bottom - top};
}

// Writes the bars painting `style` over the given row and returns how many were
// written. A row with no area paints nothing.
inline int bars(const Style style, const int x, const int y, const int width, const int height, Bar out[MAX_BARS]) {
  if (width <= 0 || height <= 0) return 0;

  if (style == SOLID) {
    out[0] = Bar{x, y, width, height};
    return 1;
  }

  const int stroke = detail::strokeFor(width, height);

  if (style == BRACKETS) {
    // Arms are capped at half the row so they can never meet and turn the four
    // corners into a plain outline box.
    const int armX = detail::clampInt(14, stroke, width / 2);
    const int armY = detail::clampInt(10, stroke, height / 2);
    const int right = x + width;
    const int bottom = y + height;

    out[0] = Bar{x, y, armX, stroke};                           // top-left, across
    out[1] = Bar{x, y, stroke, armY};                           // top-left, down
    out[2] = Bar{right - armX, y, armX, stroke};                // top-right, across
    out[3] = Bar{right - stroke, y, stroke, armY};              // top-right, down
    out[4] = Bar{x, bottom - stroke, armX, stroke};             // bottom-left, across
    out[5] = Bar{x, bottom - armY, stroke, armY};               // bottom-left, up
    out[6] = Bar{right - armX, bottom - stroke, armX, stroke};  // bottom-right, across
    out[7] = Bar{right - stroke, bottom - armY, stroke, armY};  // bottom-right, up
    return 8;
  }

  // CARET: the rule first, then the arrow drawn as columns that shorten towards
  // its tip. On a row too short to hold a readable arrow the rule stands alone.
  out[0] = Bar{x, y + height - stroke, width, stroke};
  int count = 1;

  int caretHeight = detail::clampInt(9, 0, height - stroke - 2);
  if (caretHeight % 2 == 0) caretHeight--;
  if (caretHeight < 5) return count;

  // Two pixels per column: a one-pixel arrow all but vanishes at e-ink pitch.
  constexpr int columnWidth = 2;
  const int columns = detail::clampInt((caretHeight + 1) / 2, 2, 4);
  const int caretLeft = x + detail::clampInt(4, 0, width - columns * columnWidth);
  const int caretTop = y + (height - stroke - caretHeight) / 2;
  for (int c = 0; c < columns; ++c) {
    out[count++] = Bar{caretLeft + c * columnWidth, caretTop + c, columnWidth, caretHeight - 2 * c};
  }
  return count;
}

}  // namespace selection_style
