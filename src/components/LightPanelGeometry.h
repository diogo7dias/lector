#pragma once

#include <algorithm>
#include <cstdint>

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
enum class Row : uint8_t { None = 0, Brightness, Warmth, Aux };

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
// Two columns, so an odd count leaves the last cell empty rather than stretching it.
constexpr int kActionColumns = 2;
constexpr int kMaxActions = 6;

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
  bool hasWarmth;
  bool hasAux;
  Rect toggle;
  StepRow brightness;
  StepRow warmth;  // height 0 when the board has no colour temperature
  StepRow aux;     // height 0 when the context has nothing to put there
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

inline Layout forScreen(const int screenWidth, const int lineHeight, const int readoutLineHeight,
                        const bool hasWarmth, const bool hasAux, const int actionCount) {
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
  layout.hasWarmth = hasWarmth;
  layout.hasAux = hasAux;

  int y = kSidePad;
  layout.toggle = Rect{kSidePad, y, screenWidth - kSidePad * 2, kToggleHeight};
  y += kToggleHeight + kRowGap;

  const auto place = [&](const Row row, const bool withBar) {
    StepRow out{};
    out.row = row;
    out.y = y;
    out.height = kStepRowHeight;
    const int stepY = y + (kStepRowHeight - kStepHeight) / 2;
    out.minus = Rect{minusX, stepY, stepWidth, kStepHeight};
    out.plus = Rect{minusX + stepWidth + kStepGap, stepY, stepWidth, kStepHeight};
    if (withBar) {
      out.icon = Rect{kSidePad, y, kIconWidth, kStepRowHeight};
      out.bar = Bar{barX, y + (kStepRowHeight - kTrackHeight) / 2, barWidth, kTrackHeight};
      out.value = Rect{barX + barWidth + kValueGap, y, kValueWidth, kStepRowHeight};
    } else {
      // No icon and no track: the label runs from the margin to the stepper column.
      out.value = Rect{kSidePad, y, minusX - kStepsGap - kSidePad, kStepRowHeight};
    }
    y += kStepRowHeight + kRowGap;
    return out;
  };

  layout.brightness = place(Row::Brightness, /*withBar=*/true);
  layout.warmth = hasWarmth ? place(Row::Warmth, /*withBar=*/true) : StepRow{Row::Warmth, y, 0, {}, {}, {}, {}, {}};
  layout.aux = hasAux ? place(Row::Aux, /*withBar=*/false) : StepRow{Row::Aux, y, 0, {}, {}, {}, {}, {}};

  layout.actionCount = std::clamp(actionCount, 0, kMaxActions);
  const int actionHeight = std::max(kMinActionHeight, lineHeight + kActionPadY * 2);
  const int actionWidth = (screenWidth - kSidePad * 2 - kActionGap) / kActionColumns;
  for (int i = 0; i < layout.actionCount; ++i) {
    const int column = i % kActionColumns;
    const int row = i / kActionColumns;
    layout.actions[i] = Rect{kSidePad + column * (actionWidth + kActionGap), y + row * (actionHeight + kActionGap),
                             actionWidth, actionHeight};
  }
  if (layout.actionCount > 0) {
    const int rows = (layout.actionCount + kActionColumns - 1) / kActionColumns;
    y += rows * (actionHeight + kActionGap) - kActionGap + kRowGap;
  }

  layout.readout = Rect{kSidePad, y, screenWidth - kSidePad * 2, readoutLineHeight};
  y += readoutLineHeight;

  layout.height = y + kBottomPad + kRule;
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
  for (const StepRow* row : {&layout.brightness, &layout.warmth, &layout.aux}) {
    if (row->height == 0) continue;
    if (insideRect(row->minus, x, y)) return Hit{Hit::Kind::Step, row->row, -1, -1};
    if (insideRect(row->plus, x, y)) return Hit{Hit::Kind::Step, row->row, 1, -1};
    if (insideRect(row->bar, x, y)) return Hit{Hit::Kind::Track, row->row, 0, -1};
  }

  for (int i = 0; i < layout.actionCount; ++i) {
    if (insideRect(layout.actions[i], x, y)) return Hit{Hit::Kind::Action, Row::None, 0, i};
  }
  return Hit{};
}

// Maps a touch x onto the bar's range. Past either end clamps rather than doing nothing,
// so a drag that runs off the side still parks the value at 0 or at the maximum.
inline int valueForX(const Bar& bar, const int x, const int minValue, const int maxValue) {
  if (bar.width <= 0) return minValue;
  const int offset = std::clamp(x - bar.x, 0, bar.width);
  const int range = maxValue - minValue;
  return minValue + static_cast<int>((static_cast<long long>(offset) * range + bar.width / 2) / bar.width);
}

}  // namespace light_panel
