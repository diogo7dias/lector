#pragma once

// Where the UTC offset's three fields sit: "UTC [+] [5] : [45]", centred as one
// row. Pure arithmetic, so the placement is testable without a panel. The
// screen used to walk an x cursor through hand-written gaps in the middle of
// its paint, which is where a field lands off-centre the moment a translation,
// a font or a theme changes the widths under it.
namespace offset_field_row {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Measured text widths, already padded where the field is a box.
struct Widths {
  int label = 0;    // "UTC"
  int sign = 0;     // the + / - box
  int hours = 0;    // the hours box
  int colon = 0;    // ":"
  int minutes = 0;  // the minutes box
};

// label: between "UTC" and the sign box. field: between the sign and the hours
// box. colon: on each side of the colon.
struct Gaps {
  int label = 0;
  int field = 0;
  int colon = 0;
};

struct Row {
  Rect label;
  Rect sign;
  Rect hours;
  Rect colon;
  Rect minutes;
};

inline int totalWidth(const Widths& widths, const Gaps& gaps) {
  return widths.label + gaps.label + widths.sign + gaps.field + widths.hours + gaps.colon + widths.colon + gaps.colon +
         widths.minutes;
}

// The row, centred in the container. A row wider than the container starts at
// its left edge rather than off it, so the label is never cut off.
inline Row layout(const Widths& widths, const Gaps& gaps, const int containerX, const int containerWidth, const int y,
                  const int height) {
  const int width = totalWidth(widths, gaps);
  int x = containerX + (containerWidth - width) / 2;
  if (x < containerX) x = containerX;

  Row row;
  row.label = Rect{x, y, widths.label, height};
  x += widths.label + gaps.label;
  row.sign = Rect{x, y, widths.sign, height};
  x += widths.sign + gaps.field;
  row.hours = Rect{x, y, widths.hours, height};
  x += widths.hours + gaps.colon;
  row.colon = Rect{x, y, widths.colon, height};
  x += widths.colon + gaps.colon;
  row.minutes = Rect{x, y, widths.minutes, height};
  return row;
}

}  // namespace offset_field_row
