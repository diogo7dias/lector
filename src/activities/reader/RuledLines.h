#pragma once

#include <cstdint>

class GfxRenderer;
class Page;

// Ruled Lines: a dotted rule under every text row, drawn on the BW page after the text
// and never part of the layout or the section cache. Shades are dot spacing, not grey
// levels, so they look the same on every panel and with anti-aliasing off. Ported from
// CrossInk (73e260a72), without its re-centred rows.
// ponytail: rows keep their layout, so at tight line spacing the rule meets descenders and
// breaks around them; reserve clearance in the layout (CrossInk's section rebuild) if that reads badly.
namespace ruled_lines {

enum Level : uint8_t { OFF = 0, LIGHT = 1, MEDIUM = 2, DARK = 3 };

// Clear pixels kept between the rule and any ink it meets.
constexpr int PADDING = 2;

// Light every 4th pixel, Medium every 2nd, Dark solid. 0 for Off or an unknown value.
constexpr int dotStep(const uint8_t level) { return (level >= LIGHT && level <= DARK) ? 1 << (DARK - level) : 0; }

// Plots the dots of one rule at row y from left to left + width. A dot is left out within
// PADDING of ink found in the band y - PADDING .. y + PADDING, and the pattern picks up on
// its own grid after the gap. isBlack(x, y) reads the page; plot(x, y) draws one dot.
template <typename IsBlack, typename Plot>
void drawRow(const int left, const int width, const int y, const int step, IsBlack&& isBlack, Plot&& plot) {
  if (step <= 0 || width <= 0) return;
  const int right = left + width;
  int nextDot = left;
  const auto fillUntil = [&](const int end) {
    for (; nextDot < end && nextDot < right; nextDot += step) plot(nextDot, y);
  };
  for (int x = left - PADDING; x < right + PADDING; ++x) {
    for (int dy = -PADDING; dy <= PADDING; ++dy) {
      if (!isBlack(x, y + dy)) continue;
      fillUntil(x - PADDING);
      const int firstClear = x + PADDING + 1;
      if (firstClear > nextDot) nextDot = left + ((firstClear - left + step - 1) / step) * step;
      break;
    }
  }
  fillUntil(right);
}

// Rules every text row of an EPUB page at the level in SETTINGS.ruledLines (RuledLines.cpp).
void drawPage(const GfxRenderer& renderer, const Page& page, int fontId, float compression, int marginLeft,
              int marginTop, int marginRight, int marginBottom);

}  // namespace ruled_lines
