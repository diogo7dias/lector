#pragma once

#include <algorithm>

// Where the light panel's rows and bars land. The panel is a banner-styled band pulled
// down from the top edge, holding the three controls the frontlight actually has: on/off,
// brightness, warmth. Pure geometry, so the draw code stays a draw loop and the layout
// can be tested on the host without a renderer.
//
// The band deliberately stops well short of a full screen: it is drawn over the page you
// were reading, and the point of reaching for it is to see the page change as you drag.
namespace light_panel {

enum class Row : uint8_t { None = 0, Toggle, Brightness, Warmth };

// Matches banner::PAD/RULE (components/BannerStyle.h). Repeated rather than included so
// this header stays free of the font and renderer headers the host tests do not build.
constexpr int kPad = 6;
constexpr int kRule = 2;
constexpr int kSidePad = 12;
constexpr int kRowGap = 6;
constexpr int kBarHeight = 12;
// Left column: the row's name and, on a slider row, its number. Fixed so the two bars
// line up under each other instead of each starting after its own label.
constexpr int kLabelWidth = 110;
// A bar 12 px tall is far under a fingertip, so a touch counts from anywhere in the row.
// Rows are the hit targets; the bar is only what is drawn.
constexpr int kMinBarWidth = 40;

struct Bar {
  int x;
  int y;
  int width;
  int height;
};

struct RowLayout {
  Row row;
  int y;
  int height;
  Bar bar;  // width 0 on the toggle row: it has no bar to drag
};

struct Layout {
  int x;
  int y;
  int width;
  int height;
  bool hasWarmth;
  RowLayout toggle;
  RowLayout brightness;
  RowLayout warmth;  // height 0 when the board has no colour temperature
};

inline Layout forScreen(const int screenWidth, const int lineHeight, const bool hasWarmth) {
  const int rowHeight = std::max(lineHeight, kBarHeight);
  // The label column gives way before the bar does: on a narrow screen a readable bar
  // matters more than an untruncated word.
  const int barWidth = std::max(kMinBarWidth, screenWidth - kSidePad * 2 - kLabelWidth);
  const int barX = std::min(kSidePad + kLabelWidth, screenWidth - kSidePad - barWidth);

  Layout layout{};
  layout.x = 0;
  layout.y = 0;
  layout.width = screenWidth;
  layout.hasWarmth = hasWarmth;

  int y = kPad;
  const auto place = [&](const Row row, const bool withBar) {
    RowLayout out{};
    out.row = row;
    out.y = y;
    out.height = rowHeight;
    if (withBar) out.bar = Bar{barX, y + (rowHeight - kBarHeight) / 2, barWidth, kBarHeight};
    y += rowHeight + kRowGap;
    return out;
  };

  layout.toggle = place(Row::Toggle, /*withBar=*/false);
  layout.brightness = place(Row::Brightness, /*withBar=*/true);
  if (hasWarmth) {
    layout.warmth = place(Row::Warmth, /*withBar=*/true);
  } else {
    layout.warmth = RowLayout{Row::Warmth, y, 0, Bar{}};
  }

  // y overshot by one gap after the last row; that gap becomes the bottom padding.
  layout.height = y - kRowGap + kPad + kRule;
  return layout;
}

inline bool insidePanel(const Layout& layout, const int x, const int y) {
  return x >= layout.x && x < layout.x + layout.width && y >= layout.y && y < layout.y + layout.height;
}

inline Row rowAt(const Layout& layout, const int x, const int y) {
  if (!insidePanel(layout, x, y)) return Row::None;
  const auto hits = [y](const RowLayout& row) { return row.height > 0 && y >= row.y && y < row.y + row.height; };
  if (hits(layout.toggle)) return Row::Toggle;
  if (hits(layout.brightness)) return Row::Brightness;
  if (layout.hasWarmth && hits(layout.warmth)) return Row::Warmth;
  return Row::None;
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
