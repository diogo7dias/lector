#pragma once

#include <cstdint>

// Geometry for the focused-row highlight. Every style is expressed as a short list
// of solid black rectangles so the device side stays a single fillRect loop and the
// shapes themselves can be tested on the host.
namespace selection_style {

enum Style : uint8_t {
  SOLID = 0,     // the whole row filled, text knocked out white (the original look)
  BRACKETS = 1,  // an L at each corner, text left black
  CARET = 2,     // an arrow at the left of the row's first line, text left black
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

// Bracket arm thickness. Two pixels: three read as a box being drawn around the
// row, one all but vanishes at e-ink pitch.
constexpr int strokeFor(const int width, const int height) {
  const int limit = (width < height ? width : height) / 2;
  return clampInt(2, 1, limit < 1 ? 1 : limit);
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
//
// firstLineY/firstLineHeight describe the row's FIRST line of text. The caret sits
// beside that line rather than at the row's middle, so a row that wraps onto two
// lines is marked where reading starts instead of in the gap between them. Left at
// their defaults the row itself is used, which is right for a single-line row.
inline int bars(const Style style, const int x, const int y, const int width, const int height, Bar out[MAX_BARS],
                const int firstLineY = -1, const int firstLineHeight = 0) {
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

  // CARET: the arrow alone, drawn as columns that shorten towards its tip. No rule
  // under the row -- an underline the full width of the row reads as a divider
  // between rows rather than as the mark on one of them.
  const bool haveLine = firstLineY >= 0 && firstLineHeight > 0;
  const int lineY = haveLine ? firstLineY : y;
  const int lineHeight = haveLine ? firstLineHeight : height;

  int caretHeight = detail::clampInt(9, 0, lineHeight - 2);
  if (caretHeight % 2 == 0) caretHeight--;

  // Two pixels per column: a one-pixel arrow all but vanishes at e-ink pitch.
  constexpr int columnWidth = 2;
  const int columns = detail::clampInt((caretHeight + 1) / 2, 2, 4);
  // Clear of the row's left edge, so the arrow reads as pointing at the text
  // rather than as part of a frame.
  const int caretLeft = x + detail::clampInt(6, 0, width - columns * columnWidth);

  if (caretHeight < 5) {
    // Too short for an arrow to read as one. A plain tick beside the line still
    // says which row has focus, which is the one thing that must not be dropped.
    const int tickHeight = detail::clampInt(lineHeight, 1, height);
    out[0] = Bar{caretLeft, detail::clampInt(lineY, y, y + height - tickHeight), columnWidth, tickHeight};
    return 1;
  }
  // Centred on the first line, then held inside the row.
  const int caretTop =
      detail::clampInt(lineY + (lineHeight - caretHeight) / 2, y, y + height - caretHeight);
  int count = 0;
  for (int c = 0; c < columns; ++c) {
    out[count++] = Bar{caretLeft + c * columnWidth, caretTop + c, columnWidth, caretHeight - 2 * c};
  }
  return count;
}

}  // namespace selection_style
