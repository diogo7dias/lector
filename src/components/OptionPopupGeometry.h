#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

// Geometry for the option pop-up, shared by the painter (BaseTheme::drawOptionPopup) and the
// hit-test layout in OptionPopup. Both used to compute this independently and had already
// drifted apart on the title font, so the rows a tap resolved to were a couple of pixels off
// the rows that were drawn. One function now owns it.
namespace option_popup {

// One size for the whole pop-up, title included. Declared here rather than at each call site so
// the painter and the hit-test cannot measure with different fonts.
constexpr int FONT_ID = UI_10_FONT_ID;
constexpr EpdFontFamily::Style FONT_STYLE = EpdFontFamily::REGULAR;

struct Geometry {
  int dialogX = 0;
  int dialogY = 0;
  int dialogW = 0;
  int dialogH = 0;
  int itemRectX = 0;
  int itemRectW = 0;
  int innerPadding = 0;
  int titleGap = 0;
  int lineHeight = 0;
  // Title and options wrap, never cut, so each row is as tall as its lines. rowTop and
  // rowHeight are per option; the painter and the hit test read the same numbers.
  std::vector<std::string> titleLines;
  std::vector<std::vector<std::string>> optionLines;
  std::vector<int> rowTop;
  std::vector<int> rowHeight;
};

// The pop-up does not scroll: it grows with the row count. With enough ticked rows it would
// grow past the panel and paint off both edges, which is worse than a tight layout. So when
// the natural size does not fit, the gaps give way before the rows do — first the spacing
// between rows, then the padding inside each row. Rows themselves are never dropped, because a
// row the user deliberately ticked must always be reachable.
inline Geometry compute(const GfxRenderer& renderer, const ThemeMetrics& metrics, const char* title,
                        const std::vector<std::string>& options) {
  constexpr int optionFontId = FONT_ID;
  constexpr EpdFontFamily::Style optionStyle = FONT_STYLE;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int optionCount = static_cast<int>(options.size());

  const int innerPadding = metrics.optionPopupInnerPadding;
  const int selectionHPadding = metrics.optionPopupSelectionHPadding;
  const int lineHeight = renderer.getLineHeight(optionFontId);

  // The frame is drawn outside the dialog rect, so it has to come out of the budget too.
  const int availableHeight = pageHeight - metrics.popupFrameThickness * 2;

  int maxTextWidth = renderer.getTextWidth(optionFontId, title, optionStyle);
  for (const auto& opt : options) {
    const int width = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
    if (width > maxTextWidth) maxTextWidth = width;
  }

  Geometry g;
  g.lineHeight = lineHeight;
  g.innerPadding = innerPadding;
  g.titleGap = metrics.optionPopupTitleGap;
  g.dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2) * 12 / 10,
                       pageWidth - metrics.optionPopupDialogSideMargin * 2);
  g.itemRectW = g.dialogW - innerPadding * 2;

  // Wrapped inside the row's text budget; a line that fits stays one line.
  const int textWidth = std::max(1, g.itemRectW - selectionHPadding * 2);
  g.titleLines = BaseTheme::wrapUiText(renderer, title != nullptr ? title : "", textWidth, textWidth);
  if (g.titleLines.empty()) g.titleLines.emplace_back("");
  g.optionLines.reserve(options.size());
  for (const auto& opt : options) {
    auto lines = BaseTheme::wrapUiText(renderer, opt, textWidth, textWidth);
    if (lines.empty()) lines.emplace_back("");
    g.optionLines.push_back(std::move(lines));
  }
  const int titleH = static_cast<int>(g.titleLines.size()) * lineHeight;

  int itemSpacing = metrics.optionPopupItemSpacing;
  int selectionVPadding = metrics.optionPopupSelectionVPadding;

  const auto heightFor = [&](const int spacing, const int vPadding) {
    int listHeight = 0;
    for (int i = 0; i < optionCount; ++i) {
      listHeight += static_cast<int>(g.optionLines[i].size()) * lineHeight + vPadding * 2;
      if (i > 0) listHeight += spacing;
    }
    return titleH + metrics.optionPopupTitleGap + listHeight + innerPadding * 2;
  };

  while (itemSpacing > 0 && heightFor(itemSpacing, selectionVPadding) > availableHeight) itemSpacing--;
  while (selectionVPadding > 0 && heightFor(itemSpacing, selectionVPadding) > availableHeight) selectionVPadding--;

  g.dialogH = heightFor(itemSpacing, selectionVPadding);
  g.dialogX = (pageWidth - g.dialogW) / 2;
  // Never negative: an over-tall dialog stays pinned under the frame rather than climbing off
  // the top of the panel.
  g.dialogY = std::max(metrics.popupFrameThickness, (pageHeight - g.dialogH) / 2);
  g.itemRectX = g.dialogX + innerPadding;

  g.rowTop.reserve(options.size());
  g.rowHeight.reserve(options.size());
  int y = g.dialogY + innerPadding + titleH + metrics.optionPopupTitleGap;
  for (int i = 0; i < optionCount; ++i) {
    const int h = static_cast<int>(g.optionLines[i].size()) * lineHeight + selectionVPadding * 2;
    g.rowTop.push_back(y);
    g.rowHeight.push_back(h);
    y += h + itemSpacing;
  }
  return g;
}

}  // namespace option_popup
