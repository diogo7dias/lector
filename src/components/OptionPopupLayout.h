#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Where the option pop-up's card and rows sit, from the wrapped line counts alone: pure
// arithmetic, so the painter, the hit test and the host tests read the same numbers.
//
// The contents look's card: a 2px frame 24px in from both edges, the title as a heading over a
// rule, then rows like the list's (37 tall, text 36px in, the cursor 14px in).
namespace option_popup {
constexpr int SIDE = 24;  // card to the panel edge
constexpr int FRAME = 2;
constexpr int PAD_TOP = 10;        // frame to the heading block
constexpr int PAD_BOTTOM = 12;     // last row to the frame
constexpr int TEXT_INSET = 36;     // card inner edge to the text, both sides
constexpr int MARKER_INSET = 14;   // card inner edge to the cursor
constexpr int HEAD_BASELINE = 31;  // heading block top to the first heading baseline
constexpr int HEAD_LINE = 33;      // each further heading line
constexpr int HEAD_RULE = 42;      // heading block top to its 1px rule, one line
constexpr int HEAD_H = 49;         // the block, one line: 4 above, the line, 6, rule, 6
constexpr int ROW_H = 37;
constexpr int ROW_BASELINE = 27;
constexpr int ROW_LINE = 25;  // each further line of a wrapped row

struct Geometry {
  int dialogX = 0;  // the card, frame included
  int dialogY = 0;
  int dialogW = 0;
  int dialogH = 0;
  int textX = 0;
  int textRight = 0;
  int markerX = 0;
  int headTop = 0;
  int ruleY = 0;
  int rowH = ROW_H;  // a one-line row; smaller when a long list has to squeeze
  std::vector<std::string> titleLines;
  std::vector<std::vector<std::string>> optionLines;
  std::vector<int> rowTop;
  std::vector<int> rowHeight;
};

// Pure arithmetic from the wrapped line counts, so the host tests can pin it.
// The card does not scroll: a list too long for the panel squeezes its rows toward one text
// line each rather than drop one, because a row the user deliberately ticked must stay
// reachable.
inline void place(Geometry& g, const int pageWidth, const int pageHeight, const int minRowH) {
  const int titleLines = std::max<int>(1, static_cast<int>(g.titleLines.size()));
  const int headH = HEAD_H + (titleLines - 1) * HEAD_LINE;
  const auto heightFor = [&](const int rowH) {
    int h = FRAME + PAD_TOP + headH + PAD_BOTTOM + FRAME;
    for (const auto& lines : g.optionLines) h += rowH + (static_cast<int>(lines.size()) - 1) * ROW_LINE;
    return h;
  };
  g.rowH = ROW_H;
  while (g.rowH > minRowH && heightFor(g.rowH) > pageHeight) g.rowH--;

  g.dialogX = SIDE;
  g.dialogW = pageWidth - SIDE * 2;
  g.dialogH = heightFor(g.rowH);
  g.dialogY = std::max(0, (pageHeight - g.dialogH) / 2);
  g.textX = g.dialogX + FRAME + TEXT_INSET;
  g.textRight = g.dialogX + g.dialogW - FRAME - TEXT_INSET;
  g.markerX = g.dialogX + FRAME + MARKER_INSET;
  g.headTop = g.dialogY + FRAME + PAD_TOP;
  g.ruleY = g.headTop + HEAD_RULE + (titleLines - 1) * HEAD_LINE;

  g.rowTop.clear();
  g.rowHeight.clear();
  int y = g.headTop + headH;
  for (const auto& lines : g.optionLines) {
    const int h = g.rowH + (static_cast<int>(lines.size()) - 1) * ROW_LINE;
    g.rowTop.push_back(y);
    g.rowHeight.push_back(h);
    y += h;
  }
}

// The row's first baseline, which a squeezed row keeps in proportion.
inline int rowBaseline(const Geometry& g, const int row) { return g.rowTop[row] + ROW_BASELINE * g.rowH / ROW_H; }

}  // namespace option_popup
