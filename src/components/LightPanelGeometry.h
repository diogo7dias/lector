#pragma once

#include <algorithm>
#include <cstdint>

#include "RowSlider.h"

// Where the light panel's rows, steppers and buttons land. The panel is a band pulled
// down from the top edge holding the frontlight's controls and whatever actions make
// sense where you are. Pure geometry, so the draw code stays a draw loop and the layout
// can be tested on the host without a renderer.
//
// The band deliberately stops well short of a full screen: it is drawn over the page you
// were reading, and the point of reaching for it is to see the page change as you drag.
namespace light_panel {

// The rows that hold a value. Toggle is not one: it is a button, and a hit on it means
// "flip the light", not "move something".
enum class Row : uint8_t { None = 0, Brightness, Warmth, Aux, Aux2, Aux3 };

// Matches banner::RULE (components/BannerStyle.h). Repeated rather than included so this
// header stays free of the font and renderer headers the host tests do not build.
constexpr int kRule = 2;

// One margin, used on all four sides of the band. The first build inset the sides 12 px
// and the top 6, which read as the toggle being pressed against the panel's crop.
constexpr int kSidePad = 20;
constexpr int kBottomPad = 14;
constexpr int kRowGap = 12;

// A stepper row is one fingertip tall. The steppers are the controls you use without
// looking, so they are sized to be hit blind; the track between them is what you use when
// you want a value in one move.
constexpr int kStepRowHeight = 48;
constexpr int kStepWidth = 56;
constexpr int kStepHeight = 40;
// Between the minus and the plus. They are one control in two halves, so they sit closer
// to each other than to anything else in the row.
constexpr int kStepGap = 8;
// Between the value and the steppers: wide, so the number reads as the track's rather
// than as a label on the buttons.
constexpr int kStepsGap = 18;
constexpr int kToggleHeight = 52;

// The row marker (a sun, a thermometer) and the air after it.
constexpr int kIconWidth = 30;
constexpr int kIconGap = 10;
// The number, and the air between it and the track it reports.
constexpr int kValueWidth = 44;
constexpr int kValueGap = 8;

// Thick enough to drag with a thumb rather than aim at. The first build drew 12 px and it
// was under a fingertip by a factor of three.
constexpr int kTrackHeight = 22;
// The track gives way last: on a narrow screen the steppers are shaved before it is.
constexpr int kMinBarWidth = 40;
constexpr int kMinStepWidth = 28;

// Air between the buttons, and the shortest one worth aiming at.
constexpr int kActionGap = 12;
constexpr int kMinActionHeight = 44;
constexpr int kActionPadY = 10;
// Two columns; a button left alone on the last row spans it, so a long label has room. Three
// on a landscape-wide screen: its 480 px of height cannot take a fourth row of buttons.
constexpr int kActionColumns = 2;
constexpr int kWideActionColumns = 3;
constexpr int kWideScreenWidth = 640;
constexpr int kMaxActions = 7;

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

// A Bar is a Rect that happens to be a value track. Named for what it is at the call site.
using Bar = Rect;

// One "icon, track, number, minus, plus" row.
//
// `bar` and `icon` both have width 0 on a row whose value no bar can show (Text Size in a
// book, Sort outside one). That row is its label and its two steppers, and `value` widens
// to hold the label: the steppers stay in the sliders' column, so all three rows line up.
struct StepRow {
  Row row;
  int y;
  int height;
  Rect icon;
  Bar bar;
  Rect value;
  Rect minus;
  Rect plus;
};

struct Layout {
  int x;
  int y;
  int width;
  int height;
  bool hasLight;  // false on a board with no frontlight: no toggle, brightness or warmth row
  bool hasWarmth;
  bool hasAux;
  bool hasAux2;
  bool hasAux3;
  int actionColumns;
  Rect toggle;  // zero-sized without a light
  StepRow brightness;
  StepRow warmth;  // height 0 when the board has no colour temperature
  StepRow aux;     // height 0 when the context has nothing to put there
  StepRow aux2;    // second stepper row (e.g. Library Filter below Sort); height 0 when unused
  StepRow aux3;    // third stepper row (e.g. Library View below Filter); height 0 when unused
  Rect actions[kMaxActions];
  int actionCount;
  Rect readout;
};

// What a touch landed on. One answer for the whole panel, so the caller cannot ask the
// questions in the wrong order and let a stepper read as the track behind it.
struct Hit {
  enum class Kind : uint8_t { None = 0, Toggle, Step, Track, Action };
  Kind kind = Kind::None;
  Row row = Row::None;
  int delta = 0;    // -1 or +1 on a Step, 0 otherwise
  int action = -1;  // index into Layout::actions on an Action
};

inline bool insideRect(const Rect& rect, const int x, const int y) {
  return rect.width > 0 && rect.height > 0 && x >= rect.x && x < rect.x + rect.width && y >= rect.y &&
         y < rect.y + rect.height;
}

inline Layout forScreen(const int screenWidth, const int lineHeight, const int readoutLineHeight, const bool hasWarmth,
                        const bool hasAux, const int actionCount, const bool hasLight = true,
                        const bool hasAux2 = false, const bool hasAux3 = false) {
  // The steppers give way before the track does: a track too thin to drag would leave the
  // row with nothing the steppers do not already do.
  const int stepsWidth = [&] {
    const int fixed = kSidePad * 2 + kIconWidth + kIconGap + kValueWidth + kValueGap + kStepsGap + kStepGap;
    const int room = screenWidth - fixed - kMinBarWidth;
    return std::max(kMinStepWidth, std::min(kStepWidth, room / 2)) * 2 + kStepGap;
  }();
  const int stepWidth = (stepsWidth - kStepGap) / 2;
  const int minusX = screenWidth - kSidePad - stepsWidth;
  const int barX = kSidePad + kIconWidth + kIconGap;
  const int barWidth = std::max(kMinBarWidth, minusX - kStepsGap - kValueWidth - kValueGap - barX);

  Layout layout{};
  layout.x = 0;
  layout.y = 0;
  layout.width = screenWidth;
  layout.hasLight = hasLight;
  layout.hasWarmth = hasLight && hasWarmth;
  layout.hasAux = hasAux;
  layout.hasAux2 = hasAux2;
  layout.hasAux3 = hasAux3;

  // Compact vertical rhythm only in landscape with three aux rows plus light controls so
  // the band stays under 480 px without changing any existing layout.
  const bool compactWide = hasAux3 && hasLight && screenWidth >= kWideScreenWidth;
  const int rowGap = compactWide ? 6 : kRowGap;
  const int stepRowHeight = compactWide ? 40 : kStepRowHeight;
  const int stepHeight = compactWide ? 34 : kStepHeight;
  const int toggleHeight = compactWide ? 42 : kToggleHeight;

  int y = compactWide ? 12 : kSidePad;
  if (hasLight) {
    layout.toggle = Rect{kSidePad, y, screenWidth - kSidePad * 2, toggleHeight};
    y += toggleHeight + rowGap;
  }

  const auto place = [&](const Row row, const bool withBar) {
    StepRow out{};
    out.row = row;
    out.y = y;
    out.height = stepRowHeight;
    const int stepY = y + (stepRowHeight - stepHeight) / 2;
    out.minus = Rect{minusX, stepY, stepWidth, stepHeight};
    out.plus = Rect{minusX + stepWidth + kStepGap, stepY, stepWidth, stepHeight};
    if (withBar) {
      out.icon = Rect{kSidePad, y, kIconWidth, stepRowHeight};
      out.bar = Bar{barX, y + (stepRowHeight - kTrackHeight) / 2, barWidth, kTrackHeight};
      out.value = Rect{barX + barWidth + kValueGap, y, kValueWidth, stepRowHeight};
    } else {
      // No icon and no track: the label runs from the margin to the stepper column.
      out.value = Rect{kSidePad, y, minusX - kStepsGap - kSidePad, stepRowHeight};
    }
    y += stepRowHeight + rowGap;
    return out;
  };

  layout.brightness =
      hasLight ? place(Row::Brightness, /*withBar=*/true) : StepRow{Row::Brightness, y, 0, {}, {}, {}, {}, {}};
  layout.warmth =
      layout.hasWarmth ? place(Row::Warmth, /*withBar=*/true) : StepRow{Row::Warmth, y, 0, {}, {}, {}, {}, {}};
  layout.aux = hasAux ? place(Row::Aux, /*withBar=*/false) : StepRow{Row::Aux, y, 0, {}, {}, {}, {}, {}};
  layout.aux2 = hasAux2 ? place(Row::Aux2, /*withBar=*/false) : StepRow{Row::Aux2, y, 0, {}, {}, {}, {}, {}};
  layout.aux3 = hasAux3 ? place(Row::Aux3, /*withBar=*/false) : StepRow{Row::Aux3, y, 0, {}, {}, {}, {}, {}};

  layout.actionCount = std::clamp(actionCount, 0, kMaxActions);
  const int actionHeight = compactWide ? 40 : std::max(kMinActionHeight, lineHeight + kActionPadY * 2);
  const int actionGap = compactWide ? 6 : kActionGap;
  const int columns = screenWidth >= kWideScreenWidth ? kWideActionColumns : kActionColumns;
  layout.actionColumns = columns;
  const int actionWidth = (screenWidth - kSidePad * 2 - kActionGap * (columns - 1)) / columns;
  for (int i = 0; i < layout.actionCount; ++i) {
    const int column = i % columns;
    const int row = i / columns;
    const bool aloneOnLastRow = i == layout.actionCount - 1 && column == 0;
    layout.actions[i] = Rect{kSidePad + column * (actionWidth + kActionGap), y + row * (actionHeight + actionGap),
                             aloneOnLastRow ? screenWidth - kSidePad * 2 : actionWidth, actionHeight};
  }
  if (layout.actionCount > 0) {
    const int rows = (layout.actionCount + columns - 1) / columns;
    y += rows * (actionHeight + actionGap) - actionGap + rowGap;
  }

  layout.readout = Rect{kSidePad, y, screenWidth - kSidePad * 2, readoutLineHeight};
  y += readoutLineHeight;

  layout.height = y + (compactWide ? 8 : kBottomPad) + kRule;
  return layout;
}

inline bool insidePanel(const Layout& layout, const int x, const int y) {
  return x >= layout.x && x < layout.x + layout.width && y >= layout.y && y < layout.y + layout.height;
}

inline Hit hitTest(const Layout& layout, const int x, const int y) {
  if (!insidePanel(layout, x, y)) return Hit{};
  if (insideRect(layout.toggle, x, y)) return Hit{Hit::Kind::Toggle, Row::None, 0, -1};

  // Steppers before the track, so the stepper column can never read as a drag; the track
  // is inset between them anyway, but the order is what guarantees it.
  for (const StepRow* row : {&layout.brightness, &layout.warmth, &layout.aux, &layout.aux2, &layout.aux3}) {
    if (row->height == 0) continue;
    if (insideRect(row->minus, x, y)) return Hit{Hit::Kind::Step, row->row, -1, -1};
    if (insideRect(row->plus, x, y)) return Hit{Hit::Kind::Step, row->row, 1, -1};
    // The whole row height takes the touch, not just the 22px track: a tap a little above
    // or below the bar still jumps to that point along it.
    if (insideRect(Rect{row->bar.x, row->y, row->bar.width, row->height}, x, y)) {
      return Hit{Hit::Kind::Track, row->row, 0, -1};
    }
  }

  for (int i = 0; i < layout.actionCount; ++i) {
    if (insideRect(layout.actions[i], x, y)) return Hit{Hit::Kind::Action, Row::None, 0, i};
  }
  return Hit{};
}

// Where the key cursor sits: a value row, or one button of the action grid.
struct Cursor {
  Row row = Row::None;
  int action = -1;  // index into Layout::actions when on the grid, -1 on a row

  bool onGrid() const { return action >= 0; }
  bool operator==(const Cursor& other) const { return row == other.row && action == other.action; }
};

// The value rows this layout shows, top to bottom. Returns how many.
inline int valueRows(const Layout& layout, Row (&out)[5]) {
  int count = 0;
  for (const StepRow* row : {&layout.brightness, &layout.warmth, &layout.aux, &layout.aux2, &layout.aux3}) {
    if (row->height > 0) out[count++] = row->row;
  }
  return count;
}

// Where the cursor starts: the first value row, or the first button when there is none.
inline Cursor firstCursor(const Layout& layout) {
  Row rows[5];
  if (valueRows(layout, rows) > 0) return Cursor{rows[0], -1};
  return layout.actionCount > 0 ? Cursor{Row::None, 0} : Cursor{};
}

// Up/Down: one value row at a time, then into the grid a grid row at a time (Down off the
// last value row lands on the first button). Stops at either end rather than wrapping, like
// every list on the device.
inline Cursor moveVertical(const Layout& layout, const Cursor cursor, const int delta) {
  Row rows[5];
  const int rowCount = valueRows(layout, rows);
  const int columns = std::max(1, layout.actionColumns);
  if (cursor.onGrid()) {
    const int next = cursor.action + delta * columns;
    if (next < 0) return rowCount > 0 ? Cursor{rows[rowCount - 1], -1} : cursor;
    const int lastGridRow = (layout.actionCount - 1) / columns;
    if (next >= layout.actionCount) {
      // A short last grid row: Down from above it still gets there, on its last button.
      return cursor.action / columns < lastGridRow ? Cursor{Row::None, layout.actionCount - 1} : cursor;
    }
    return Cursor{Row::None, next};
  }
  int index = 0;
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i] == cursor.row) index = i;
  }
  const int next = index + delta;
  if (next >= rowCount) return layout.actionCount > 0 ? Cursor{Row::None, 0} : cursor;
  if (next < 0 || rowCount == 0) return cursor;
  return Cursor{rows[next], -1};
}

// Left/Right on the grid: the next or previous button, stopping at either end.
inline Cursor moveHorizontal(const Layout& layout, const Cursor cursor, const int delta) {
  if (!cursor.onGrid()) return cursor;
  return Cursor{Row::None, std::clamp(cursor.action + delta, 0, layout.actionCount - 1)};
}

// Maps a touch x onto the bar's range: the settings rows' drag-bar rule, unstepped. Past
// either end clamps rather than doing nothing, so a drag that runs off the side still
// parks the value at 0 or at the maximum.
inline int valueForX(const Bar& bar, const int x, const int minValue, const int maxValue) {
  return row_slider::valueForX(row_slider::Bar{bar.x, bar.y, bar.width, bar.height}, x, minValue, maxValue, 1);
}

}  // namespace light_panel
