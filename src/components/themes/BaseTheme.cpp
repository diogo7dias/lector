#include "BaseTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/BannerStyle.h"
#include "components/HeaderTitle.h"
#include "components/HintBandGeometry.h"
#include "components/ListScrollPolicy.h"
#include "components/ListScrollbar.h"
#include "components/OptionPopupGeometry.h"
#include "components/RowHitTest.h"
#include "components/UITheme.h"
#include "components/WrappedListWindow.h"
#include "components/icons/bookmark.h"
#include "components/themes/SelectionStyle.h"
#include "fontIds.h"
#include "util/StringUtils.h"

// Internal constants
namespace {
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;
constexpr int bookmarkStatusIconWidth = 16;
constexpr int bookmarkStatusIconHeight = 14;
constexpr int bookmarkStatusIconGap = 4;
constexpr int bookmarkStatusIconTopCrop = 2;

// Greedy word-wrap of input in the UI_10 font. Line 0 is wrapped to firstLineMaxWidth
// (leaving room for an inline [NN%] badge), later lines to restMaxWidth. An over-long
// single word is broken by character; a line that still will not fit is truncated.
std::vector<std::string> wrapText(const GfxRenderer& renderer, const std::string& input, int firstLineMaxWidth,
                                  int restMaxWidth) {
  std::vector<std::string> lines;
  if (input.empty()) {
    lines.push_back("");
    return lines;
  }

  size_t i = 0;
  while (i < input.size()) {
    while (i < input.size() && input[i] == ' ') i++;
    if (i >= input.size()) break;

    const int maxWidth = lines.empty() ? firstLineMaxWidth : restMaxWidth;
    std::string line;
    size_t lineEndPos = i;
    while (lineEndPos < input.size()) {
      size_t wordEnd = lineEndPos;
      while (wordEnd < input.size() && input[wordEnd] != ' ') wordEnd++;
      const std::string word = input.substr(lineEndPos, wordEnd - lineEndPos);
      const std::string candidate = line.empty() ? word : (line + " " + word);

      if (renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) <= maxWidth) {
        line = candidate;
        lineEndPos = wordEnd;
        while (lineEndPos < input.size() && input[lineEndPos] == ' ') lineEndPos++;
        continue;
      }

      if (line.empty()) {
        size_t fit = 1;
        while (fit < word.size() && renderer.getTextWidth(UI_10_FONT_ID, word.substr(0, fit + 1).c_str()) <= maxWidth) {
          fit++;
        }
        line = word.substr(0, fit);
        lineEndPos += fit;
      }
      break;
    }

    if (line.empty()) {
      line = renderer.truncatedText(UI_10_FONT_ID, input.substr(i).c_str(), maxWidth);
      lines.push_back(line);
      break;
    }

    lines.push_back(line);
    i = lineEndPos;
  }

  if (lines.empty()) {
    lines.push_back(renderer.truncatedText(UI_10_FONT_ID, input.c_str(), firstLineMaxWidth));
  }
  return lines;
}

// Draw a centered "N more above/below" indicator badge. formatKey's value must contain
// a single "%d".
void drawMoreIndicator(const GfxRenderer& renderer, int count, StrId formatKey, int centerX, int centerW, int y,
                       int rowLineHeight) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), I18N.get(formatKey), count);
  const int textW = renderer.getTextWidth(UI_10_FONT_ID, buf);
  const int badgeW = textW + 24;
  const int badgeH = rowLineHeight + 6;
  const int badgeX = centerX + (centerW - badgeW) / 2;
  renderer.fillRect(badgeX, y, badgeW, badgeH);
  const int textX = badgeX + (badgeW - textW) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, y + 3, buf, false);
}

void drawBookmarkStatusIcon(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int bytesPerRow = bookmarkStatusIconWidth / 8;
  for (int row = 0; row < bookmarkStatusIconHeight; ++row) {
    for (int col = 0; col < bookmarkStatusIconWidth; ++col) {
      const uint8_t byte = BookmarkStatusIcon[(row + bookmarkStatusIconTopCrop) * bytesPerRow + col / 8];
      const uint8_t mask = 1U << (7 - (col % 8));
      renderer.drawPixel(x + col, y + row, (byte & mask) != 0);
    }
  }
}

}  // namespace

int BaseTheme::batteryClusterWidth(const GfxRenderer& renderer) {
  // "100%" rather than the live value: the reserve must not change width as the
  // battery drains, or the things placed against it would shuffle sideways.
  return batteryRightPadding + BaseMetrics::values.batteryWidth + batteryPercentSpacing +
         renderer.getTextWidth(batteryPercentFontId, "100%");
}

int BaseTheme::batteryIconTop(const GfxRenderer& renderer, const Rect& rect, const int fontId) {
  // Centre the icon in the line box of the text drawn at rect.y. Expressed from the
  // font's own metrics rather than a fixed offset so the Ubuntu family (bound for
  // Arabic/Hebrew, taller than Cozette) lands right too. For the default Cozette 12
  // this evaluates to the +6 the code used before.
  return rect.y + std::max(0, (renderer.getLineHeight(fontId) - rect.height) / 2);
}

void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  const int maxFillWidth = rect.width - 5;
  const int fillHeight = rect.height - 4;
  if (maxFillWidth <= 0 || fillHeight <= 0) {
    return;
  }
  // +1 to round up so we always fill at least one pixel
  int filledWidth = percentage * maxFillWidth / 100 + 1;
  if (filledWidth > maxFillWidth) {
    filledWidth = maxFillWidth;
  }

  // When charging, ensure minimum fill so lightning bolt is fully visible
  constexpr int minFillForBolt = 8;
  if (charging && filledWidth < minFillForBolt) {
    filledWidth = std::min(minFillForBolt, maxFillWidth);
  }

  renderer.fillRect(rect.x + 2, rect.y + 2, filledWidth, fillHeight);

  if (charging) {
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  }
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage,
                                const int fontId) const {
  // Left aligned: icon on left, percentage on right (reader mode). fontId sizes the
  // percentage text; the v2 status bar passes UI_10 so the drawn width matches the
  // segment width it reserved, while UI headers keep the SMALL_FONT_ID default.
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = batteryIconTop(renderer, rect, fontId);

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(fontId, rect.x + batteryPercentSpacing + rect.width, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Right aligned: percentage on left, icon on right (UI headers)
  // rect.x is already positioned for the icon (drawHeader calculated it)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = batteryIconTop(renderer, rect, batteryPercentFontId);

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(batteryPercentFontId, percentageText.c_str());
    renderer.drawText(batteryPercentFontId, rect.x - textWidth - batteryPercentSpacing, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current,
                                const size_t total) const {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

// Centre a button-hint label inside its box. A label that fits is drawn on the
// single baseline it always was; one too wide used to overflow the button border
// and run into the neighbouring hint, and now wraps to at most two centred lines
// (wrappedText() ellipsises anything that still doesn't fit). Shared so every
// theme's drawButtonHints() gets the same behaviour.
void BaseTheme::drawHintLabel(GfxRenderer& renderer, const int fontId, const char* label, const int x,
                              const int boxWidth, const int boxTop, const int boxHeight, const int singleLineYOffset) {
  constexpr int textPadding = 4;  // keeps a wrapped label off the button's border
  const int maxTextWidth = boxWidth - (textPadding * 2);

  const int textWidth = renderer.getTextWidth(fontId, label);
  if (textWidth <= maxTextWidth) {
    renderer.drawText(fontId, x + (boxWidth - 1 - textWidth) / 2, boxTop + singleLineYOffset, label);
    return;
  }

  // A label too wide to fit drops to the smaller UI font before it is allowed to wrap.
  // Two UI_10 lines stack as tall as the button itself (2 x 19 + 2 = 40 against a
  // 40-pixel box), so the second line landed on the border and lost its bottom rows —
  // "Reading Stats" on the home screen showed "Reading" over a clipped "Stats". One
  // smaller line beats two clipped ones; only a label too wide even for that wraps, and
  // by then the smaller glyphs leave room for both lines.
  constexpr int lineGap = 2;
  int wrapFontId = fontId;
  if (fontId != SMALL_FONT_ID) {
    const int smallWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    if (smallWidth <= maxTextWidth) {
      renderer.drawText(SMALL_FONT_ID, x + (boxWidth - 1 - smallWidth) / 2,
                        boxTop + (boxHeight - renderer.getTextHeight(SMALL_FONT_ID)) / 2, label);
      return;
    }
    wrapFontId = SMALL_FONT_ID;
  }

  // Spaced by the glyph height, not getLineHeight() — that returns the font's
  // full advanceY (leading included), which stacks two lines taller than the
  // button and clips the second one.
  const int step = renderer.getTextHeight(wrapFontId) + lineGap;
  const auto lines = renderer.wrappedText(wrapFontId, label, maxTextWidth, 2);
  const int block = static_cast<int>(lines.size()) * step - lineGap;
  int lineY = boxTop + std::max(1, (boxHeight - block) / 2);
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(wrapFontId, line.c_str());
    renderer.drawText(wrapFontId, x + (boxWidth - 1 - lineWidth) / 2, lineY, line.c_str());
    lineY += step;
  }
}

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  constexpr int textYOffset = 7;  // Distance from top of button to text baseline
  const hint_band::Band band = hintBand(renderer);
  const char* labels[hint_band::kSlotCount] = {btn1, btn2, btn3, btn4};

  hint_band::Painted& painted = hint_band::lastPainted();
  painted.band = band;
  painted.valid = true;

  for (int i = 0; i < hint_band::kSlotCount; i++) {
    painted.labelled[i] = labels[i] != nullptr && labels[i][0] != '\0';
  }

  for (int i = 0; i < hint_band::kSlotCount; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const hint_band::Slot slot = hint_band::slot(band, i);
      renderer.fillRect(slot.x, slot.y, slot.width, slot.height, false);
      if (band.touch) {
        // The touch band tiles the full width, so neighbouring slots share an edge and
        // a per-slot drawRect() painted that edge twice: the dividers between the
        // buttons came out two pixels wide against the one-pixel line along the top.
        // Draw the outline by hand and let each slot own only its right-hand divider;
        // the leftmost slot draws the outer left edge, and the rightmost one's divider
        // is the outer right edge.
        renderer.fillRect(slot.x, slot.y, slot.width, 1);                   // top
        renderer.fillRect(slot.x, slot.y + slot.height - 1, slot.width, 1);  // bottom
        renderer.fillRect(slot.x + slot.width - 1, slot.y, 1, slot.height);  // divider / right edge
        if (i == 0) renderer.fillRect(slot.x, slot.y, 1, slot.height);       // outer left edge
      } else {
        renderer.drawRect(slot.x, slot.y, slot.width, slot.height);
      }
      drawHintLabel(renderer, UI_10_FONT_ID, labels[i], slot.x, slot.width, slot.y, slot.height, textYOffset);
    }
  }

  renderer.setOrientation(orig_orientation);
}

hint_band::Band BaseTheme::hintBand(const GfxRenderer& renderer) const {
  // Measured in Portrait, which is the orientation drawButtonHints paints in and the one
  // MappedInputManager hands back logical tap coordinates in.
  return hint_band::Band{renderer.getScreenWidth(), renderer.getScreenHeight(),
                         UITheme::getInstance().getMetrics().buttonHintsHeight, gpio.hasTouch(), gpio.deviceIsX3()};
}

void BaseTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = BaseMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 80;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 4;

  if (gpio.deviceIsX3()) {
    // X3 layout: Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      const int leftX = buttonMargin;
      renderer.drawRect(leftX, x3ButtonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int textX = leftX + (buttonWidth - textHeight) / 2;
      const int textY = x3ButtonY + (buttonHeight + textWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonMargin - buttonWidth;
      renderer.drawRect(rightX, x3ButtonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int textX = rightX + (buttonWidth - textHeight) / 2;
      const int textY = x3ButtonY + (buttonHeight + textWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, bottomBtn);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    constexpr int topButtonY = 345;
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonMargin - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);
      renderer.drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);
    }

    if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
      renderer.drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
                        topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1, topButtonY + 2 * buttonHeight - 1);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topButtonY + i * buttonHeight;
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
        const int textX = x + (buttonWidth - textHeight) / 2;
        const int textY = y + (buttonHeight + textWidth) / 2;
        renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, labels[i]);
      }
    }
  }
}

int BaseTheme::getListRowStep(bool hasSubtitle) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
}

int BaseTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

bool BaseTheme::drawSelection(const GfxRenderer& renderer, const Rect rect, const Rect* spans,
                              const int spanCount) const {
  const selection_style::Style style = selection_style::fromSetting(SETTINGS.selectionStyle);
  const selection_style::Bar row{rect.x, rect.y, rect.width, rect.height};

  // The first span is the row's first line of text, which is where the caret goes.
  // Without one (a cover, a tab) it falls back to the row.
  const int firstLineY = (spans != nullptr && spanCount > 0) ? spans[0].y : -1;
  const int firstLineHeight = (spans != nullptr && spanCount > 0) ? spans[0].height : 0;

  const auto paint = [&renderer, firstLineY, firstLineHeight](const selection_style::Style s,
                                                              const selection_style::Bar& area) {
    selection_style::Bar painted[selection_style::MAX_BARS];
    const int count =
        selection_style::bars(s, area.x, area.y, area.width, area.height, painted, firstLineY, firstLineHeight);
    for (int i = 0; i < count; ++i) {
      renderer.fillRect(painted[i].x, painted[i].y, painted[i].width, painted[i].height);
    }
  };

  if (style == selection_style::BRACKETS && spans != nullptr && spanCount > 0) {
    bool bracketed = false;
    for (int i = 0; i < spanCount; ++i) {
      const selection_style::Bar grown =
          selection_style::inflatedSpan({spans[i].x, spans[i].y, spans[i].width, spans[i].height}, row);
      if (grown.width <= 0 || grown.height <= 0) continue;  // e.g. a row with no value text
      paint(style, grown);
      bracketed = true;
    }
    // A caller that measured nothing usable still needs its row marked.
    if (!bracketed) paint(style, row);
    return false;
  }

  paint(style, row);
  return selection_style::invertsText(style);
}

void BaseTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed, int itemFontId,
                         const std::function<bool(int index)>& rowIsHeader, int* scrollOffset) const {
  const auto& listMetrics = UITheme::getInstance().getMetrics();
  int rowHeight = (rowSubtitle != nullptr) ? listMetrics.listWithSubtitleRowHeight : listMetrics.listRowHeight;
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  // Scrolling callers own their window start; paging callers get it snapped to a
  // whole page, exactly as before.
  int windowStart;
  bool showUpArrow;
  bool showDownArrow;
  if (scrollOffset != nullptr) {
    *scrollOffset = list_scroll::nextScrollOffset(*scrollOffset, selectedIndex, pageItems, itemCount);
    windowStart = *scrollOffset;
    // Here the arrows mean "more above" and "more below", which can differ.
    showUpArrow = windowStart > 0;
    showDownArrow = windowStart + pageItems < itemCount;
  } else {
    windowStart = selectedIndex / pageItems * pageItems;
    const int totalPages = (itemCount + pageItems - 1) / pageItems;
    showUpArrow = showDownArrow = totalPages > 1;
  }

  // The scroll indicator: a track down the right-hand edge with a thumb as long as the
  // fraction of the list on screen. It replaced a pair of up/down arrows, which said only
  // that there was more in that direction, never how much or how far in you were.
  const bool scrollable = showUpArrow || showDownArrow;
  if (scrollable) {
    const list_scrollbar::Bar bar = list_scrollbar::forList(rect.y, rect.height, itemCount, windowStart, pageItems);
    if (bar.visible) {
      const int barX = list_scrollbar::trackX(rect.x, rect.width);
      // Knocked out of whatever is behind it first: a heading bar or a selected row is
      // solid black, and a black thumb on black is no indicator at all.
      renderer.fillRect(barX - list_scrollbar::kOutlineWidth, bar.trackY - list_scrollbar::kOutlineWidth,
                        list_scrollbar::kWidth + list_scrollbar::kOutlineWidth * 2,
                        bar.trackHeight + list_scrollbar::kOutlineWidth * 2, false);
      // The track is dithered, the thumb solid: on a one-bit panel that is the only way
      // to show the thumb's position against the track it slides in.
      renderer.fillRectDither(barX, bar.trackY, list_scrollbar::kWidth, bar.trackHeight, Color::LightGray);
      renderer.fillRect(barX, bar.thumbY, list_scrollbar::kWidth, bar.thumbHeight);
    }
  }

  // Rows stop short of the track when there is one, so a long value never runs under it.
  int contentWidth = rect.width - (scrollable ? list_scrollbar::kReservedWidth : 5);
  // Only the solid style paints over the row, so only then does the row's own text
  // have to come out white. Resolved when the selected row is reached, because the
  // bracket style needs that row's label and value measured first.
  bool selectionInvertsText = true;
  // Rows drawn against untouched paper, including the selected one under a
  // non-solid style.
  const auto drawnOnPaper = [&](const int index) { return index != selectedIndex || !selectionInvertsText; };
  constexpr int minValueGap = 10;

  row_hit::Rows& hitRows = row_hit::lastRows();

  // Draw all items
  for (int i = windowStart; i < itemCount && i < windowStart + pageItems; i++) {
    const int itemY = rect.y + (i - windowStart) * rowHeight;

    // Section heading: a filled bar spanning the list, label centred and knocked out
    // white. Deliberately the same fill the selected row uses — a heading is never
    // landable (the caller's navigation steps past it), so the two can never be on
    // screen in a way that makes one look like the other, and one solid band reads as
    // a divider far better at e-ink contrast than a hairline rule does.
    if (rowIsHeader != nullptr && rowIsHeader(i)) {
      // Bracketed like the screen title above it, for the same reason: the UI font has
      // no bold face, so the brackets are what marks a line as a label rather than a row.
      const std::string headingText = header_title::decorate(rowTitle(i).c_str());
      renderer.fillRect(rect.x, itemY - 2, rect.width, rowHeight);
      const int headingW = renderer.getTextWidth(itemFontId, headingText.c_str());
      const int headingX = rect.x + std::max(0, (rect.width - headingW) / 2);
      const int headingY = itemY + std::max(0, (rowHeight - renderer.getLineHeight(itemFontId)) / 2);
      renderer.drawText(itemFontId, headingX, headingY, headingText.c_str(), /*black=*/false);
      continue;
    }

    hitRows.add(i, rect.x, itemY, rect.width, rowHeight);

    int rowTextWidth = contentWidth - BaseMetrics::values.contentSidePadding * 2;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        int maxValW = std::max(0, rowTextWidth - 40 - minValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
        int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + minValueGap;
        rowTextWidth -= valueWidth;
      }
    }

    auto itemName = rowTitle(i);
    auto font = itemFontId;
    auto item = renderer.truncatedText(font, itemName.c_str(), rowTextWidth);

    // Rows are finger-height on a touch board (listRowHeight rises from 40 px to 56),
    // so the text block is centred in the row rather than parked against its top edge,
    // which is what leaves the taller row looking top-heavy and half empty.
    const int subtitleOffset = 22;
    const int blockHeight = (rowSubtitle != nullptr) ? subtitleOffset + renderer.getLineHeight(SMALL_FONT_ID)
                                                     : renderer.getLineHeight(font);
    const int textY = itemY + std::max(0, (rowHeight - blockHeight) / 2);

    // Where the value will land, needed here so the selection can bracket it before
    // any of the row's text is drawn over.
    const int valueTextWidth = valueText.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
    const int valueX = rect.x + contentWidth - BaseMetrics::values.contentSidePadding - valueTextWidth;
    const int valueY = (rowSubtitle != nullptr) ? textY + 10 : textY;

    if (i == selectedIndex) {
      const int titleX = rect.x + BaseMetrics::values.contentSidePadding;
      const Rect spans[2] = {
          Rect(titleX, textY, renderer.getTextWidth(font, item.c_str()), renderer.getLineHeight(font)),
          Rect(valueX, valueY, valueTextWidth, renderer.getLineHeight(UI_10_FONT_ID)),
      };
      selectionInvertsText =
          drawSelection(renderer, Rect(rect.x, rect.y + (i - windowStart) * rowHeight - 2, rect.width, rowHeight),
                        spans, valueText.empty() ? 1 : 2);
    }

    renderer.drawText(font, rect.x + BaseMetrics::values.contentSidePadding, textY, item.c_str(), drawnOnPaper(i));

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && drawnOnPaper(i)) {
      const int titleWidth = renderer.getTextWidth(font, item.c_str());
      const int lineH = renderer.getLineHeight(font);
      const int tx = rect.x + BaseMetrics::values.contentSidePadding;
      for (int py = textY; py < textY + lineH; py++)
        for (int px = tx; px < tx + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, rect.x + BaseMetrics::values.contentSidePadding,
                          textY + subtitleOffset, subtitle.c_str(), drawnOnPaper(i));
      }
    }

    if (!valueText.empty()) {
      renderer.drawText(UI_10_FONT_ID, valueX, valueY, valueText.c_str(), drawnOnPaper(i));
    }
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  // Hide last battery draw. Both the width and the height come from the cluster's own
  // metrics: a hardcoded box was narrower than the percentage text in the taller UI
  // font, and started below the text's tallest glyphs, so a partial redraw left
  // fragments of the previous reading behind.
  const int clusterWidth = batteryClusterWidth(renderer);
  renderer.fillRect(rect.x + rect.width - clusterWidth, rect.y, clusterWidth,
                    renderer.getLineHeight(batteryPercentFontId) + 10, false);

  // The percentage is always drawn: the setting that used to hide it was removed, and its
  // default was "never hide", so this is the behaviour every existing device already had.
  constexpr bool showBatteryPercentage = true;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - batteryRightPadding - BaseMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, BaseMetrics::values.batteryWidth, BaseMetrics::values.batteryHeight},
                   showBatteryPercentage);

  if (title) {
    int padding = rect.width - batteryX + BaseMetrics::values.batteryWidth;
    // Same size as the rows under it and as the home screen's own text. A step larger
    // read as bold next to everything else on the screen. The brackets are what marks it
    // as the title instead: see components/HeaderTitle.h for why not bold.
    const std::string decoratedTitle = header_title::decorate(title);
    auto truncatedTitle = renderer.truncatedText(UI_10_FONT_ID, decoratedTitle.c_str(),
                                                 rect.width - padding * 2 - BaseMetrics::values.contentSidePadding * 2,
                                                 EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, rect.y + 5, truncatedTitle.c_str(), true, EpdFontFamily::REGULAR);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(
        SMALL_FONT_ID, subtitle, rect.width - BaseMetrics::values.contentSidePadding * 2, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    // The subtitle (the firmware version, on the Settings screen) sits at the BOTTOM right,
    // above the button hints. Its Y used to be the constant 738, measured against one panel:
    // the hints band starts at screenHeight - buttonHintsHeight, which is 752 on the X3's
    // 792-row portrait screen, so the version ran into the Down hint. Derive it instead, and
    // keep a gap so descenders never touch the band either.
    constexpr int subtitleGap = 6;
    int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
    renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
    const int subtitleY = renderer.getScreenHeight() - viewBottom -
                          UITheme::getInstance().getMetrics().buttonHintsHeight -
                          renderer.getLineHeight(SMALL_FONT_ID) - subtitleGap;
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - BaseMetrics::values.contentSidePadding - truncatedSubtitleWidth, subtitleY,
                      truncatedSubtitle.c_str(), true);
  }
}

void BaseTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  constexpr int maxListValueWidth = 200;

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;
  int rightSpace = BaseMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - BaseMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + 10;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - BaseMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
}

// How far the focused-tab highlight bleeds past its label on each side. Shared by the
// draw loop that paints it and the two loops that decide whether a tab still fits.
static constexpr int tabHighlightBleed = 3;

size_t BaseTheme::firstVisibleTab(const GfxRenderer& renderer, const Rect rect,
                                  const std::vector<TabInfo>& tabs) const {
  if (tabs.empty()) return 0;

  const int spacing = BaseMetrics::values.tabSpacing;
  // Measured against the space the bar actually draws into: it starts one side padding
  // in and may run to the far edge. Deliberately not a symmetric margin. The widest
  // shipped bar is the Portuguese (PT) Text Settings tabs at 446px of the 460px this
  // leaves; a symmetric 2 * contentSidePadding would budget 440px and drop a tab there,
  // trading a real tab for a margin the bar never had.
  const int available = rect.width - BaseMetrics::values.contentSidePadding;

  int wanted = 0;
  size_t selectedIndex = 0;
  for (size_t i = 0; i < tabs.size(); i++) {
    if (i > 0) wanted += spacing;
    wanted += renderer.getTextWidth(UI_10_FONT_ID, tabs[i].label, EpdFontFamily::REGULAR);
    if (tabs[i].selected) selectedIndex = i;
  }
  // Everything fits. This is the path every shipped configuration takes: measured
  // across all 31 translations and both built-in UI fonts, the widest of the three tab
  // bars is 446px against 460px available. The branch below is a guard, not a fix for
  // anything currently on screen — it becomes reachable with a wider SD-card UI font,
  // a translation of the reader-menu tab labels (they are English-only today), or a
  // sixth tab.
  if (wanted <= available) return 0;

  // Overflow. Start from the selected tab and widen the window leftwards while there is
  // room, so the selected tab is always drawn whole. The window is filled leftward only,
  // so it is the tabs to the RIGHT of the selected one that are dropped first — moving
  // rightwards therefore reveals the next tab only once it is selected.
  size_t first = selectedIndex;
  int used = renderer.getTextWidth(UI_10_FONT_ID, tabs[selectedIndex].label, EpdFontFamily::REGULAR);
  while (first > 0) {
    const int widened =
        used + spacing + renderer.getTextWidth(UI_10_FONT_ID, tabs[first - 1].label, EpdFontFamily::REGULAR);
    if (widened > available) break;
    used = widened;
    first--;
  }
  return first;
}

void BaseTheme::drawTabBar(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  constexpr int underlineHeight = 2;  // Height of selection underline
  constexpr int underlineGap = 4;     // Gap between text and underline

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const size_t first = firstVisibleTab(renderer, rect, tabs);
  const int rightEdge = rect.x + rect.width;

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;

  for (size_t i = first; i < tabs.size(); i++) {
    const auto& tab = tabs[i];
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);

    // Stop before painting past the right edge: a tab that does not fit whole is left
    // out rather than drawn half. The first tab is always drawn, so a label wider than
    // the whole bar still shows (clipped) instead of the bar coming out empty.
    // The bleed is charged to every tab, not just the focused one, so that
    // tabIndexFromPoint can apply the identical test without being told which tab has
    // focus. Costing 3px on tabs that will not draw a highlight is the price of the two
    // loops staying in lockstep.
    if (i > first && currentX + textWidth + tabHighlightBleed > rightEdge) break;

    // Draw underline for selected tab. A tab that is selected but does not hold focus
    // always keeps its plain underline; the user's selection style applies only to the
    // focused tab, and the caret style falls back to the solid highlight here because
    // its own rule would be indistinguishable from that unfocused underline.
    bool inverted = false;
    if (tab.selected) {
      if (selected) {
        const Rect tabRect(currentX - tabHighlightBleed, rect.y, textWidth + 2 * tabHighlightBleed,
                           lineHeight + underlineGap);
        if (selection_style::fromSetting(SETTINGS.selectionStyle) == selection_style::BRACKETS) {
          inverted = drawSelection(renderer, tabRect);
        } else {
          renderer.fillRect(tabRect.x, tabRect.y, tabRect.width, tabRect.height);
          inverted = true;
        }
      } else {
        renderer.fillRect(currentX, rect.y + lineHeight + underlineGap, textWidth, underlineHeight);
      }
    }

    // Draw tab label
    renderer.drawText(UI_10_FONT_ID, currentX, rect.y, tab.label, !inverted, EpdFontFamily::REGULAR);

    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }
}

bool BaseTheme::tabIndexFromPoint(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                                  const int x, const int y, int& index) const {
  if (tabs.empty() || y < rect.y || y >= rect.y + rect.height) {
    return false;
  }

  const size_t first = firstVisibleTab(renderer, rect, tabs);
  const int rightEdge = rect.x + rect.width;

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;
  for (size_t i = first; i < tabs.size(); i++) {
    const auto& tab = tabs[i];
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);
    // Same cut-off as drawTabBar, bleed included: an undrawn tab must not answer to a
    // touch.
    if (i > first && currentX + textWidth + tabHighlightBleed > rightEdge) break;
    const int left = (i == first) ? rect.x : currentX - BaseMetrics::values.tabSpacing / 2;
    const int right = currentX + textWidth + BaseMetrics::values.tabSpacing / 2;
    if (x >= left && x < right) {
      index = static_cast<int>(i);
      return true;
    }
    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }

  return false;
}

// Draw the "Recent Book" cover card on the home screen
// TODO: Refactor method to make it cleaner, split into smaller methods
void BaseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasContinueReading = !recentBooks.empty();
  const bool bookSelected = hasContinueReading && selectorIndex == 0;
  // The card is a cover plus its label boxes, not a list row, so only the solid style
  // flips them all to white-on-black. Brackets and the caret mark the card's outline
  // and leave the cover art, the title and the "Continue Reading" chip as they are.
  const bool cardInverted =
      bookSelected && selection_style::fromSetting(SETTINGS.selectionStyle) == selection_style::SOLID;

  // --- Top "book" card for the current title (selectorIndex == 0) ---
  // When there's no cover image, use fixed size (half screen)
  // When there's cover image, adapt width to image aspect ratio, keep height fixed at 400px
  const int baseHeight = rect.height;  // Fixed height (400px)

  int bookWidth, bookX;
  bool hasCoverImage = false;

  if (hasContinueReading && !recentBooks[0].coverBmpPath.empty()) {
    // Try to get actual image dimensions from BMP header
    const std::string coverBmpPath =
        UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, BaseMetrics::values.homeCoverHeight);

    HalFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        hasCoverImage = true;
        const int imgWidth = bitmap.getWidth();
        const int imgHeight = bitmap.getHeight();

        // Calculate width based on aspect ratio, maintaining baseHeight
        if (imgWidth > 0 && imgHeight > 0) {
          const float aspectRatio = static_cast<float>(imgWidth) / static_cast<float>(imgHeight);
          bookWidth = static_cast<int>(baseHeight * aspectRatio);

          // Ensure width doesn't exceed reasonable limits (max 90% of screen width)
          const int maxWidth = static_cast<int>(rect.width * 0.9f);
          if (bookWidth > maxWidth) {
            bookWidth = maxWidth;
          }
        } else {
          bookWidth = rect.width / 2;  // Fallback
        }
      }
    }
  }

  if (!hasCoverImage) {
    // No cover: use half screen size
    bookWidth = rect.width / 2;
  }

  bookX = rect.x + (rect.width - bookWidth) / 2;
  const int bookY = rect.y;
  const int bookHeight = baseHeight;

  // Bookmark dimensions (used in multiple places)
  const int bookmarkWidth = bookWidth / 8;
  const int bookmarkHeight = bookHeight / 5;
  const int bookmarkX = bookX + bookWidth - bookmarkWidth - 10;
  const int bookmarkY = bookY + 5;

  // Draw book card regardless, fill with message based on `hasContinueReading`
  {
    // Draw cover image as background if available (inside the box)
    // Only load from SD on first render, then use stored buffer

    if (hasContinueReading && !recentBooks[0].coverBmpPath.empty() && !coverRendered) {
      const std::string coverBmpPath =
          UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, BaseMetrics::values.homeCoverHeight);

      // First time: load cover from SD and render
      HalFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          LOG_DBG("THEME", "Rendering bmp");

          // Draw the cover image (bookWidth and bookHeight already match image aspect ratio)
          renderer.drawBitmap(bitmap, bookX, bookY, bookWidth, bookHeight);

          // Draw border around the card
          renderer.drawRect(bookX, bookY, bookWidth, bookHeight);

          // No bookmark ribbon when cover is shown - it would just cover the art

          // Store the buffer with cover image for fast navigation
          coverBufferStored = storeCoverBuffer();
          coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer

          // First render: if selected, draw selection indicators now
          // Solid keeps the nested border it always drew. The other styles mark the
          // card once, further down, where the title box has been measured.
          if (cardInverted) {
            LOG_DBG("THEME", "Drawing selection");
            renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
            renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
          }
        }
      }
    }

    if (!bufferRestored && !coverRendered) {
      // No cover image: draw border or fill, plus bookmark as visual flair
      if (bookSelected) {
        drawSelection(renderer, Rect(bookX, bookY, bookWidth, bookHeight));
        if (!cardInverted) renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
      } else {
        renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
      }

      // Draw bookmark ribbon when no cover image (visual decoration)
      if (hasContinueReading) {
        const int notchDepth = bookmarkHeight / 3;
        const int centerX = bookmarkX + bookmarkWidth / 2;

        const int xPoints[5] = {
            bookmarkX,                  // top-left
            bookmarkX + bookmarkWidth,  // top-right
            bookmarkX + bookmarkWidth,  // bottom-right
            centerX,                    // center notch point
            bookmarkX                   // bottom-left
        };
        const int yPoints[5] = {
            bookmarkY,                                // top-left
            bookmarkY,                                // top-right
            bookmarkY + bookmarkHeight,               // bottom-right
            bookmarkY + bookmarkHeight - notchDepth,  // center notch point
            bookmarkY + bookmarkHeight                // bottom-left
        };

        // Draw bookmark ribbon (inverted if selected)
        renderer.fillPolygon(xPoints, yPoints, 5, !cardInverted);
      }
    }

    // If buffer was restored, draw selection indicators if needed
    if (bufferRestored && cardInverted && coverRendered) {
      // Draw selection border (no bookmark inversion needed since cover has no bookmark)
      renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
      renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
    } else if (!coverRendered && !bufferRestored) {
      // Selection border already handled above in the no-cover case
    }
  }

  if (hasContinueReading) {
    const std::string& lastBookTitle = recentBooks[0].title;
    const std::string& lastBookAuthor = recentBooks[0].author;

    // Invert text colors based on selection state:
    // - With cover: selected = white text on black box, unselected = black text on white box
    // - Without cover: selected = white text on black card, unselected = black text on white card

    auto lines = renderer.wrappedText(UI_12_FONT_ID, lastBookTitle.c_str(), bookWidth - 40, 3);

    // Book title text
    int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * static_cast<int>(lines.size());
    if (!lastBookAuthor.empty()) {
      totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    }

    // Vertically center the title block within the card
    int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

    const auto truncatedAuthor = lastBookAuthor.empty()
                                     ? std::string{}
                                     : renderer.truncatedText(UI_10_FONT_ID, lastBookAuthor.c_str(), bookWidth - 40);

    // If cover image was rendered, draw box behind title and author
    if (coverRendered) {
      constexpr int boxPadding = 8;
      // Calculate the max text width for the box
      int maxTextWidth = 0;
      for (const auto& line : lines) {
        const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str());
        if (lineWidth > maxTextWidth) {
          maxTextWidth = lineWidth;
        }
      }
      if (!truncatedAuthor.empty()) {
        const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, truncatedAuthor.c_str());
        if (authorWidth > maxTextWidth) {
          maxTextWidth = authorWidth;
        }
      }

      const int boxWidth = maxTextWidth + boxPadding * 2;
      const int boxHeight = totalTextHeight + boxPadding * 2;
      const int boxX = rect.x + (rect.width - boxWidth) / 2;
      const int boxY = titleYStart - boxPadding;

      // Draw box (inverted when selected: black box instead of white)
      renderer.fillRect(boxX, boxY, boxWidth, boxHeight, cardInverted);
      // Draw border around the box (inverted when selected: white border instead of black)
      renderer.drawRect(boxX, boxY, boxWidth, boxHeight, !cardInverted);
      // The one place a non-solid style marks the card, so a restored cover buffer and
      // a freshly rendered one behave alike and neither gets marked twice. Guarded on
      // the selection itself: without it, Solid would fill every unselected card black.
      if (bookSelected && !cardInverted) {
        const Rect titleBox(boxX, boxY, boxWidth, boxHeight);
        drawSelection(renderer, Rect(bookX, bookY, bookWidth, bookHeight), &titleBox, 1);
      }
    }

    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, line.c_str(), !cardInverted);
      titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!truncatedAuthor.empty()) {
      titleYStart += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, truncatedAuthor.c_str(), !cardInverted);
    }

    // "Continue Reading" label at the bottom
    const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
    if (coverRendered) {
      // Draw box behind "Continue Reading" text (inverted when selected: black box instead of white)
      const char* continueText = tr(STR_CONTINUE_READING);
      const int continueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, continueText);
      constexpr int continuePadding = 6;
      const int continueBoxWidth = continueTextWidth + continuePadding * 2;
      const int continueBoxHeight = renderer.getLineHeight(UI_10_FONT_ID) + continuePadding;
      const int continueBoxX = rect.x + (rect.width - continueBoxWidth) / 2;
      const int continueBoxY = continueY - continuePadding / 2;
      renderer.fillRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, cardInverted);
      renderer.drawRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, !cardInverted);
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, continueText, !cardInverted);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, tr(STR_CONTINUE_READING), !cardInverted);
    }
  } else {
    // No book to continue reading
    const int y =
        bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NO_OPEN_BOOK));
    renderer.drawCenteredText(UI_10_FONT_ID, y + renderer.getLineHeight(UI_12_FONT_ID), tr(STR_START_READING));
  }
}

// Defined below, next to the home list it was written for.
void badgeChipMetrics(const GfxRenderer& renderer, const char* text, int* chipW, int* textDx);

ListVisibility BaseTheme::drawWrappedList(const GfxRenderer& renderer, const Rect rect, const int itemCount,
                                          const int selectedIndex, const int scrollOffset,
                                          const std::function<std::string(int index)>& rowTitle,
                                          const std::function<std::string(int index)>& rowValue,
                                          const std::function<std::string(int index)>& rowBadge) const {
  if (itemCount <= 0 || !rowTitle) {
    return {0, 0, itemCount > 0 ? itemCount : 0};
  }

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int rowGap = 2;
  constexpr int rowPadY = 4;    // vertical breathing room inside a row
  constexpr int valueGap = 10;  // gap between the wrapped title and the right-aligned value
  const int contentX = rect.x + BaseMetrics::values.contentSidePadding;
  const int contentW = rect.width - BaseMetrics::values.contentSidePadding * 2;

  // Reserve a band at both ends for the "N more" badges whenever the list can scroll, so
  // rows never sit under one.
  const int indicatorH = (itemCount > 1) ? lineHeight + 8 : 0;
  const int listTop = rect.y + indicatorH;
  const int listHeight = rect.height - indicatorH * 2;
  if (contentW <= 0 || listHeight < lineHeight) {
    return {0, 0, itemCount};
  }

  constexpr int badgeGap = 6;  // blank between the chip and the first character of the title

  struct Row {
    int index;
    std::vector<std::string> lines;
    std::string value;
    int valueW;
    int height;
    std::string badge;
    int badgeW;       // 0 = no badge on this row
    int badgeTextDx;  // where the text sits inside the chip (see badgeChipMetrics)
  };

  // Measuring wraps the title, so it is only ever done for rows near the window — never
  // for a whole directory, which can hold thousands of entries.
  auto measure = [&](const int index) {
    Row row{index, {}, {}, 0, 0, {}, 0, 0};
    if (rowValue) {
      row.value = rowValue(index);
      if (!row.value.empty()) {
        row.valueW = renderer.getTextWidth(UI_10_FONT_ID, row.value.c_str()) + valueGap;
      }
    }
    if (rowBadge) {
      row.badge = rowBadge(index);
      if (!row.badge.empty()) {
        badgeChipMetrics(renderer, row.badge.c_str(), &row.badgeW, &row.badgeTextDx);
        row.badgeW += badgeGap;
      }
    }
    // The chip eats width from every line, because every line is drawn at the first
    // line's x. Letting the continuation lines run back to the left margin, under the
    // chip, leaves the text block with a ragged left edge.
    const int textW = std::max(1, contentW - row.badgeW);
    // Never let the value squeeze the first line to nothing: below a third of the width the
    // title wraps under the value instead.
    const int firstLineW = std::max(textW / 3, textW - row.valueW);
    row.lines = wrapText(renderer, rowTitle(index), firstLineW, textW);
    if (row.lines.empty()) row.lines.emplace_back("");
    row.height = static_cast<int>(row.lines.size()) * lineHeight + rowPadY;
    return row;
  };

  // Measuring wraps a title, so the same row is asked for its height more than once as the
  // window is worked out. Cache the last few: the window walk only ever touches rows near
  // the window, and re-wrapping each of them two or three times is the one cost here that
  // is pure waste.
  std::vector<std::pair<int, int>> heightCache;
  auto heightOf = [&](const int index) {
    for (const auto& entry : heightCache) {
      if (entry.first == index) return entry.second;
    }
    const int h = measure(index).height;
    if (heightCache.size() >= 32) heightCache.erase(heightCache.begin());
    heightCache.emplace_back(index, h);
    return h;
  };

  const wrapped_list::Window win =
      wrapped_list::window(itemCount, selectedIndex, scrollOffset, listHeight, rowGap, heightOf);

  std::vector<Row> rows;
  rows.reserve(static_cast<size_t>(win.count));
  for (int i = win.first; i < win.first + win.count && i < itemCount; i++) rows.push_back(measure(i));
  if (rows.empty()) rows.push_back(measure(std::clamp(win.first, 0, itemCount - 1)));

  const int firstVisible = rows.front().index;
  const int lastVisible = rows.back().index;

  if (firstVisible > 0) {
    drawMoreIndicator(renderer, firstVisible, StrId::STR_MORE_ABOVE, rect.x, rect.width, rect.y, lineHeight);
  }
  if (lastVisible < itemCount - 1) {
    drawMoreIndicator(renderer, itemCount - 1 - lastVisible, StrId::STR_MORE_BELOW, rect.x, rect.width,
                      rect.y + rect.height - indicatorH, lineHeight);
  }

  int rowY = listTop;
  row_hit::Rows& wrappedHitRows = row_hit::lastRows();
  for (const Row& row : rows) {
    const bool selected = row.index == selectedIndex;
    const int valueX = contentX + contentW - (row.valueW - valueGap);
    // One highlight over the whole measured height, so a row spanning several lines is
    // marked as a single block. `inverted` is true only under the solid style, which is
    // the only one that paints over the row's own text and its badge chip. The bracket
    // style hugs the wrapped title block and the value separately.
    bool inverted = false;
    if (selected) {
      const int titleX = contentX + row.badgeW;
      const Rect spans[2] = {
          Rect(titleX, rowY + 3, contentX + contentW - titleX - (row.valueW > 0 ? row.valueW : 0),
               static_cast<int>(row.lines.size()) * lineHeight),
          Rect(valueX, rowY + 3, row.valueW - valueGap, lineHeight),
      };
      inverted = drawSelection(renderer, Rect(rect.x, rowY, rect.width, row.height), spans, row.valueW > 0 ? 2 : 1);
    }
    if (row.valueW > 0) {
      renderer.drawText(UI_10_FONT_ID, valueX, rowY + 3, row.value.c_str(), !inverted);
    }
    int textX = contentX;
    if (row.badgeW > 0) {
      // The chip flips with the row so it stays legible on both grounds: black chip with
      // white text on an unselected row, white chip with black text on the inverted one.
      const int chipW = row.badgeW - badgeGap;
      const int chipH = lineHeight + 2;
      const int chipY = rowY + 3 + (lineHeight - chipH) / 2;
      renderer.fillRect(contentX, chipY, chipW, chipH, !inverted);
      renderer.drawText(UI_10_FONT_ID, contentX + row.badgeTextDx, chipY + (chipH - lineHeight) / 2, row.badge.c_str(),
                        inverted);
      textX = contentX + row.badgeW;
    }
    int baselineY = rowY + 3;
    for (const std::string& line : row.lines) {
      renderer.drawText(UI_10_FONT_ID, textX, baselineY, line.c_str(), !inverted);
      baselineY += lineHeight;
    }
    wrappedHitRows.add(row.index, rect.x, rowY, rect.width, row.height);
    rowY += row.height + rowGap;
  }

  return {firstVisible, lastVisible, itemCount};
}

void BaseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon, const int itemIndexBase) const {
  const auto& menuMetrics = UITheme::getInstance().getMetrics();
  row_hit::Rows& menuHitRows = row_hit::lastRows();
  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = BaseMetrics::values.verticalSpacing + rect.y +
                      static_cast<int>(i) * (menuMetrics.menuRowHeight + menuMetrics.menuSpacing);

    const bool selected = selectedIndex == i;

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY =
        tileY + (menuMetrics.menuRowHeight - lineHeight) / 2;  // vertically centered assuming y is top of text

    // Unselected tiles already carry an outline, so the highlight has to read against
    // one. Brackets hug the tile's own label and the caret rule doubles the tile's
    // bottom edge; both stay legible without the tile inverting.
    menuHitRows.add(itemIndexBase + i, rect.x + menuMetrics.contentSidePadding, tileY,
                    rect.width - menuMetrics.contentSidePadding * 2, menuMetrics.menuRowHeight);

    const Rect tile(rect.x + menuMetrics.contentSidePadding, tileY, rect.width - menuMetrics.contentSidePadding * 2,
                    menuMetrics.menuRowHeight);
    bool inverted = false;
    if (selected) {
      const Rect labelSpan(textX, textY, textWidth, lineHeight);
      inverted = drawSelection(renderer, tile, &labelSpan, 1);
      if (!inverted) renderer.drawRect(tile.x, tile.y, tile.width, tile.height);
    } else {
      renderer.drawRect(tile.x, tile.y, tile.width, tile.height);
    }
    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, !inverted);
  }
}

Rect BaseTheme::drawBannerStrip(const GfxRenderer& renderer, const char* message) const {
  const int w = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(banner::FONT_ID);
  const int h = banner::PAD * 2 + lineHeight;

  // Physical top crop (X4 crops ~9px, X3 crops 0) via the renderer's oriented viewable
  // inset, the same construction the wake banners use: the black backing starts at row
  // 0 so nothing white shows above the band, while the text and the rule sit below the
  // crop where they cannot be clipped. Starting the backing at the theme's topPadding
  // instead left a white gap along the top edge.
  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const int y = viewTop;

  renderer.fillRect(0, 0, w, y + h, true);                             // black to the physical edge
  renderer.fillRect(0, y + h - banner::RULE, w, banner::RULE, false);  // rule on the page-facing edge
  renderer.drawCenteredText(banner::FONT_ID, y + banner::PAD, message, false, EpdFontFamily::REGULAR);
  return Rect{0, y, w, h};
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const Rect layout = drawBannerStrip(renderer, message);
  renderer.displayBuffer();
  return layout;
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int barHeight = metrics.popupProgressBarHeight;
  const int barWidth =
      std::max(0, layout.width - metrics.popupMarginX * 2);  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  // Centered in the blank between the text's line box and the rule, so the bar rides
  // inside the band rather than pushing it taller. Derived from the band's own geometry
  // so it follows banner::PAD instead of having to be retuned whenever the band changes.
  const int gapTop = layout.y + banner::PAD + renderer.getLineHeight(banner::FONT_ID);
  const int gapBottom = layout.y + layout.height - banner::RULE;
  const int barY = gapTop + std::max(0, (gapBottom - gapTop - barHeight) / 2);
  if (barWidth <= 0 || barHeight <= 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const int scaledProgress = metrics.popupProgressClampPercent ? std::clamp(progress, 0, 100) : progress;
  const int fillWidth = barWidth * scaledProgress / 100;

  if (metrics.popupProgressDrawOutline) {
    renderer.drawRect(barX, barY, barWidth, barHeight, 1, metrics.popupProgressOutlineInverted);
  }
  if (fillWidth > 0) {
    renderer.fillRect(barX, barY, fillWidth, barHeight, metrics.popupProgressFillInverted);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BaseTheme::drawStatusBarV2(GfxRenderer& renderer, const StatusBarData& data) const {
  // Two independent halves. The text items need the status bar switched on; the edge
  // progress bars can also be kept alive on their own by sbOffBar, in which case this
  // function draws the bars and returns before touching any text.
  const bool drawText = SETTINGS.statusBarEnabled();
  if (!drawText && !SETTINGS.progressBarsVisible()) return;

  const int f = UI_10_FONT_ID;
  const auto& metrics = UITheme::getInstance().getMetrics();
  int mt, mr, mb, ml;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const int leftEdge = metrics.statusBarHorizontalMargin + ml + 1;
  const int rightEdge = screenW - metrics.statusBarHorizontalMargin - mr;
  const int bandWidth = rightEdge - leftEdge;
  const int lineH = renderer.getLineHeight(f);

  // --- Progress bars --------------------------------------------------------
  // Drawn before the text so the bars-only path can return early. topTextY /
  // bottomTextY fall out of the same stacking, so the text band always sits inside
  // whatever the bars left free — matching the heights UITheme reserved.
  //
  // Flush by default. With sbFloatingBar on, one small margin lifts the band off
  // the outer edge and pulls both ends in by the same amount, so the bar reads as
  // a floating pill. The margin is paid once per band (the gap is outside the
  // stack), which is exactly what UITheme reserves.
  const bool outlined = SETTINGS.sbBarOutline != 0;
  const int barPx = statusBarDrawThicknessPx(SETTINGS.activeBarThickness(), outlined);
  const int floatMargin = SETTINGS.floatingBarMarginPx();
  // Edge bars bleed past both ends of the logical screen. On the X4 Pro the panel sits
  // slightly off-centre behind its bezel, so a bar drawn exactly to x=0 stops short of
  // the glass on one side and its starting edge shows as a stub at low percentages.
  // Overdrawing costs nothing (fillRect clips) and makes an empty bar start off-screen
  // and a full one run off the other end, which is what reads as edge to edge. A
  // floating bar wants its gap, so it takes no bleed.
  const int edgeBleed = floatMargin > 0 ? 0 : kEdgeBarBleedPx;
  const int barLeft = ml + floatMargin - edgeBleed;
  const int barMaxW = std::max(1, screenW - ml - mr - floatMargin * 2 + edgeBleed * 2);
  auto clampPct = [](int p) { return p < 0 ? 0 : (p > 100 ? 100 : p); };
  // How far the bar nearest an edge is stretched to reach the panel itself. The
  // viewable margins hold content clear of the bezel, which leaves a strip of paper
  // between a "flush" bar and the edge of the screen; filling it is what makes the
  // bar read as flush. A floating bar wants that gap, so it keeps it.
  const int stretchTop = floatMargin > 0 ? 0 : mt;
  const int stretchBottom = floatMargin > 0 ? 0 : mb;
  auto drawEdgeBar = [&](int y, int pct, int stretchUp = 0, int stretchDown = 0) {
    const int top = y - stretchUp;
    const int height = barPx + stretchUp + stretchDown;
    if (!outlined) {
      const int w = barMaxW * clampPct(pct) / 100;
      if (w > 0) renderer.fillRect(barLeft, top, w, height, true);
      return;
    }
    // Outlined: a 1px frame over the whole track, the fill inset inside it so the
    // empty remainder stays readable as a track.
    renderer.drawRect(barLeft, top, barMaxW, height, 1, true);
    const int innerW = barMaxW - 2;
    const int innerH = height - 2;
    if (innerW <= 0 || innerH <= 0) return;
    const int w = innerW * clampPct(pct) / 100;
    if (w > 0) renderer.fillRect(barLeft + 1, top + 1, w, innerH, true);
  };

  const bool anyTopBar = SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_TOP ||
                         (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_TOP && data.hasChapters);
  const bool anyBottomBar = SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_BOTTOM ||
                            (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_BOTTOM && data.hasChapters);

  // Top edge: book bar then chapter bar; text band below them.
  int topStack = mt + (anyTopBar ? floatMargin : 0);
  // Only the bar nearest the edge reaches for it; a second bar stacks under the first.
  bool topOutermost = true;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_TOP) {
    drawEdgeBar(topStack, data.bookPercent, stretchTop);
    topStack += barPx;
    topOutermost = false;
  }
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_TOP && data.hasChapters) {
    drawEdgeBar(topStack, data.chapterPercent, topOutermost ? stretchTop : 0);
    topStack += barPx;
  }
  const int topTextY = topStack + 2;

  // Bottom edge: bars along the bottom; text band above them.
  int bottomStack = screenH - mb - (anyBottomBar ? floatMargin : 0);
  bool bottomOutermost = true;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_BOTTOM) {
    bottomStack -= barPx;
    drawEdgeBar(bottomStack, data.bookPercent, 0, stretchBottom);
    bottomOutermost = false;
  }
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_BOTTOM && data.hasChapters) {
    bottomStack -= barPx;
    drawEdgeBar(bottomStack, data.chapterPercent, 0, bottomOutermost ? stretchBottom : 0);
  }
  const int bottomTextY = bottomStack - lineH - 2;

  if (!drawText) return;  // bars-only: the status bar is hidden, sbOffBar kept them

  // Separator between co-anchored items: a drawn vertical bar with equal gaps on
  // each side. A " | " string looked lopsided because the '|' glyph sits
  // off-centre in its monospace cell (wide gap before, tight after).
  const int sepGap = 4;   // even gap each side of the bar
  const int sepBarW = 1;  // bar thickness
  const int sepW = sepGap + sepBarW + sepGap;
  // Always shown — see the note on showBatteryPercentage in drawHeader().
  constexpr bool showBattery = true;

  // --- Build the bar on the STACK: one fixed segment array per anchor. No heap in
  // this render path (it runs on the lock-holding, stack-tight render task). Short
  // items format into local char buffers; the title points at the caller's string.
  // The layout + reflow live in the pure, host-tested `statusbar` module. ---
  using statusbar::Seg;
  statusbar::BarLayout L{};
  int titleAnchorIdx = -1;  // set when the title item is actually placed
  auto push = [&](uint8_t anchor, bool chapterOnly, const char* text, int width, bool isBattery) {
    if (anchor == CrossPointSettings::SB_ANCHOR_OFF) return;
    if (chapterOnly && !data.hasChapters) return;  // chapter items hide on chapterless books
    const int idx = static_cast<int>(anchor) - 1;  // TL(1)..BR(6) -> 0..5
    if (idx < 0 || idx >= statusbar::kAnchorCount || L.counts[idx] >= statusbar::kMaxPerAnchor) return;
    L.buckets[idx][L.counts[idx]++] = Seg{text, width, isBattery};
  };

  char batBuf[8] = "";
  char clkBuf[12] = "";
  char pageBuf[20] = "";
  char bookPctBuf[10] = "";
  char chapPctBuf[10] = "";
  char chapNumBuf[24] = "";
  char sessionBuf[16] = "";

  // Battery (icon + optional %)
  {
    int w = metrics.batteryWidth;
    if (showBattery) {
      snprintf(batBuf, sizeof(batBuf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
      w += batteryPercentSpacing + renderer.getTextWidth(f, batBuf);
    }
    push(SETTINGS.sbBatteryPos, false, batBuf, w, true);
  }
  // Clock (X3 RTC only). Only read the RTC when the clock is actually placed, so a
  // clock-off config doesn't do an I2C transaction every frame.
  if (SETTINGS.sbClockPos != CrossPointSettings::SB_ANCHOR_OFF && halClock.isAvailable() &&
      halClock.formatTime(clkBuf, sizeof(clkBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
    push(SETTINGS.sbClockPos, false, clkBuf, renderer.getTextWidth(f, clkBuf), false);
  }
  // Title (points at the caller's string; truncated at draw time if it overflows).
  // Chapter source falls back to the book title on a chapterless book (TXT, flat
  // XTC) so the title never silently vanishes there; hence the item is not
  // chapter-only.
  {
    const bool chapterSrc = SETTINGS.sbTitleSource == CrossPointSettings::SB_TITLE_CHAPTER;
    const char* title = (chapterSrc && data.hasChapters) ? data.chapterTitle.c_str() : data.bookTitle.c_str();
    if (title[0] != '\0') {
      push(SETTINGS.sbTitlePos, false, title, renderer.getTextWidth(f, title), false);
      const int idx = static_cast<int>(SETTINGS.sbTitlePos) - 1;
      if (SETTINGS.sbTitlePos != CrossPointSettings::SB_ANCHOR_OFF && idx >= 0 && idx < statusbar::kAnchorCount)
        titleAnchorIdx = idx;  // reflow pivots on where the greedy title landed
    }
  }
  // Page in chapter ("3/40" or "8 left")
  if (SETTINGS.sbPageFormat == CrossPointSettings::SB_PAGE_LEFT) {
    const int remaining = data.chapterPages - data.chapterPage;
    snprintf(pageBuf, sizeof(pageBuf), "%d left", remaining > 0 ? remaining : 0);
  } else {
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", data.chapterPage, data.chapterPages);
  }
  // Page item is NOT chapter-only: on a chapterless book (TXT, flat XTC) the
  // reader fills chapterPage/chapterPages with BOOK page/total so it still shows.
  push(SETTINGS.sbPagePos, false, pageBuf, renderer.getTextWidth(f, pageBuf), false);
  // Book % ("B:20%"), Chapter % ("C:60%"), Chapter number ("Ch 2/12")
  snprintf(bookPctBuf, sizeof(bookPctBuf), "B:%d%%", data.bookPercent);
  push(SETTINGS.sbBookPctPos, false, bookPctBuf, renderer.getTextWidth(f, bookPctBuf), false);
  snprintf(chapPctBuf, sizeof(chapPctBuf), "C:%d%%", data.chapterPercent);
  push(SETTINGS.sbChapterPctPos, true, chapPctBuf, renderer.getTextWidth(f, chapPctBuf), false);
  snprintf(chapNumBuf, sizeof(chapNumBuf), "Ch %d/%d", data.chapterNum, data.chapterTotal);
  push(SETTINGS.sbChapterNumPos, true, chapNumBuf, renderer.getTextWidth(f, chapNumBuf), false);
  // Pages turned this sitting ("+12"). The plus carries "since you sat down" on its
  // own, so no letter has to be decoded — unlike B:/C:, which only work because a
  // percent sign follows and the letter merely picks which percent.
  // Hidden when the reader reports no session, so it never sits at 0 pretending to count.
  if (data.sessionPages >= 0) {
    snprintf(sessionBuf, sizeof(sessionBuf), "+%d", data.sessionPages);
    push(SETTINGS.sbSessionPagesPos, false, sessionBuf, renderer.getTextWidth(f, sessionBuf), false);
  }

  // Pages left in the paragraph this page starts in (">P.2"). Almost always 0: a
  // paragraph that does not run past the page bottom has nothing to warn about. The
  // point of it is the other case, where it says how far the current thought runs on.
  char paraBuf[12];
  if (data.paragraphPagesLeft >= 0) {
    snprintf(paraBuf, sizeof(paraBuf), ">P.%d", data.paragraphPagesLeft);
    push(SETTINGS.sbParaPagesPos, false, paraBuf, renderer.getTextWidth(f, paraBuf), false);
  }

  // --- Reflow: a greedy (truncate-OFF) title bumps overlapping same-band
  // neighbours into the opposite band. Pure + allocation-free (see StatusBar.cpp).
  // The opposite band may only *receive* bumped items when it already reserves
  // height (has native text) — the band heights are computed pre-reflow from native
  // anchors, so a bump into an unreserved band would draw over the reading text.
  if (titleAnchorIdx >= 0 && SETTINGS.sbTitleTruncate == 0) {
    const int destBase = (titleAnchorIdx < 3) ? 3 : 0;
    const bool destReserved = L.counts[destBase] > 0 || L.counts[destBase + 1] > 0 || L.counts[destBase + 2] > 0;
    statusbar::reflowTitle(L, titleAnchorIdx, /*titleTruncate=*/false, bandWidth, sepW, destReserved);
  }

  auto clusterW = [&](int idx) { return statusbar::clusterWidth(L, idx, sepW); };

  // --- Draw one anchor cluster (align: 0 left edge, 1 centered, 2 right edge) ---
  auto drawAnchor = [&](int idx, int align, int y) {
    if (L.counts[idx] == 0) return;
    const int total = clusterW(idx);

    // A lone centre segment (the title) that overflows clips to the space the
    // left/right clusters of its band leave free.
    if (align == 1 && L.counts[idx] == 1) {
      const bool top = idx < 3;
      const int lw = clusterW(top ? 0 : 3);
      const int rw = clusterW(top ? 2 : 5);
      const int avail = bandWidth - lw - rw - 20;
      if (avail > 0 && total > avail) {
        // Only reached by a truncate-ON title (the greedy truncate-OFF title is
        // drawn wrapped above and its bucket emptied) -> clip with an ellipsis.
        std::string clipped = renderer.truncatedText(f, L.buckets[idx][0].text, avail);
        const int cx = leftEdge + lw + (bandWidth - lw - rw - renderer.getTextWidth(f, clipped.c_str())) / 2;
        renderer.drawText(f, cx, y, clipped.c_str());
        return;
      }
    }

    int x = (align == 0) ? leftEdge : (align == 2) ? (rightEdge - total) : (leftEdge + (bandWidth - total) / 2);
    for (int i = 0; i < L.counts[idx]; i++) {
      if (i > 0) {
        // Vertical bar centred in the separator advance, equal gap each side.
        x += sepGap;
        renderer.drawLine(x, y + 2, x, y + lineH - 3, true);
        x += sepBarW + sepGap;
      }
      const Seg& s = L.buckets[idx][i];
      if (s.isBattery) {
        drawBatteryLeft(renderer, Rect{x, y, metrics.batteryWidth, metrics.batteryHeight}, showBattery, f);
      } else {
        renderer.drawText(f, x, y, s.text);
      }
      x += s.width;
    }
  };

  // A greedy (truncate-off) lone title wraps across as many lines as it needs and
  // is drawn here, aligned to its anchor column (left/centre/right). The band's
  // extra height was reserved by getStatusBarV2TitleLines at inset time. We empty
  // its bucket so the generic pass below skips it. A truncate-ON title (or one
  // sharing its anchor) falls through to drawAnchor's single-line ellipsis clip.
  if (titleAnchorIdx >= 0 && SETTINGS.sbTitleTruncate == 0 && L.counts[titleAnchorIdx] == 1) {
    const int col = titleAnchorIdx % 3;
    const bool top = titleAnchorIdx < 3;
    const auto lines = renderer.wrappedText(f, L.buckets[titleAnchorIdx][0].text, bandWidth, 6);
    const int n = static_cast<int>(lines.size());
    for (int i = 0; i < n; i++) {
      const int lw = renderer.getTextWidth(f, lines[i].c_str());
      const int x = (col == 0) ? leftEdge : (col == 2) ? (rightEdge - lw) : (leftEdge + (bandWidth - lw) / 2);
      const int y = top ? (topTextY + i * lineH) : (bottomTextY - (n - 1 - i) * lineH);
      renderer.drawText(f, x, y, lines[i].c_str());
    }
    L.counts[titleAnchorIdx] = 0;  // consumed; skip in the generic pass
  }

  drawAnchor(0, 0, topTextY);     // TL
  drawAnchor(1, 1, topTextY);     // TC
  drawAnchor(2, 2, topTextY);     // TR
  drawAnchor(3, 0, bottomTextY);  // BL
  drawAnchor(4, 1, bottomTextY);  // BC
  drawAnchor(5, 2, bottomTextY);  // BR
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}

void BaseTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                              int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? metrics.textFieldCursorThickness : metrics.textFieldNormalThickness;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY,
                      rect.x + contentStartX + contentWidth + metrics.textFieldLineEndOffset, lineY, thickness, true);
  } else {
    const int lineW = textWidth + metrics.textFieldHorizontalPadding * 2;
    const int lineStart = rect.x + (rect.width - lineW) / 2;
    renderer.drawLine(lineStart, lineY, lineStart + lineW + metrics.textFieldLineEndOffset, lineY, thickness, true);
  }
}

void BaseTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                                int selectedIndex, bool leftAlign) const {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // One size for the whole popup, the same one the menu rows behind it use. The title
  // used to be a step larger, which read as bold against the options under it.
  constexpr int optionFontId = option_popup::FONT_ID;
  constexpr EpdFontFamily::Style optionStyle = option_popup::FONT_STYLE;

  // Shared with OptionPopup's hit-test layout, so a tap always resolves to the row it landed on.
  const auto geometry = option_popup::compute(renderer, metrics, title, options);
  const int innerPadding = geometry.innerPadding;
  const int selectionHPadding = metrics.optionPopupSelectionHPadding;
  const int rowHeight = geometry.rowHeight;
  const int rowPitch = geometry.rowPitch;
  const int titleLineHeight = geometry.titleLineHeight;

  const int optionCount = static_cast<int>(options.size());
  const int dialogW = geometry.dialogW;
  const int dialogH = geometry.dialogH;
  const int dialogX = geometry.dialogX;
  const int dialogY = geometry.dialogY;

  const int frameThickness = metrics.popupFrameThickness;
  const int frameRadius = metrics.popupCornerRadius;

  if (frameRadius > 0) {
    renderer.fillRoundedRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                             dialogH + frameThickness * 2, frameRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, frameRadius, Color::Black);
    renderer.fillRoundedRect(dialogX + frameThickness, dialogY + frameThickness, dialogW - frameThickness * 2,
                             dialogH - frameThickness * 2,
                             frameRadius - frameThickness > 0 ? frameRadius - frameThickness : 0, Color::White);
  } else {
    renderer.fillRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                      dialogH + frameThickness * 2, true);
    renderer.fillRect(dialogX, dialogY, dialogW, dialogH, false);
  }

  int y = dialogY + innerPadding;

  renderer.drawCenteredText(optionFontId, y, title, true, optionStyle);
  y += titleLineHeight;

  if (metrics.optionPopupTitleSeparator) {
    const int sepY = y + metrics.optionPopupTitleGap / 2;
    renderer.drawLine(dialogX + innerPadding, sepY, dialogX + dialogW - innerPadding, sepY, true);
  }

  y += metrics.optionPopupTitleGap;

  const int itemRectX = geometry.itemRectX;
  const int itemRectW = geometry.itemRectW;
  const int selectionRadius = metrics.optionPopupSelectionRadius;
  const int optionLineHeight = renderer.getLineHeight(optionFontId);
  const selection_style::Style selectionStyle = selection_style::fromSetting(SETTINGS.selectionStyle);

  for (int i = 0; i < optionCount; i++) {
    const int itemY = geometry.firstItemY + i * rowPitch;
    const bool selected = (i == selectedIndex);
    const char* labelText = options[i].c_str();

    // Under a non-solid style the selected row keeps the popup's own background and
    // gets brackets or a caret on top, so the theme's rounded/light-grey selection
    // treatment applies to the solid style only.
    const bool paintedOver = selectionStyle == selection_style::SOLID;
    if (metrics.optionPopupDrawAllRows || (selected && paintedOver)) {
      Color rowColor;
      if (selected && paintedOver) {
        rowColor = metrics.optionPopupSelectionLight ? Color::LightGray : Color::Black;
      } else {
        // Including the selected row under a non-solid style: it keeps the popup's own
        // background and takes its brackets or caret on top.
        rowColor = Color::White;
      }
      if (selectionRadius > 0) {
        renderer.fillRoundedRect(itemRectX, itemY, itemRectW, rowHeight, selectionRadius, rowColor);
      } else {
        renderer.fillRect(itemRectX, itemY, itemRectW, rowHeight, rowColor == Color::Black);
      }
    }
    const int textW = renderer.getTextWidth(optionFontId, labelText, optionStyle);
    const int textY = itemY + (rowHeight - optionLineHeight) / 2;
    const int textX = leftAlign ? itemRectX + selectionHPadding : itemRectX + (itemRectW - textW) / 2;

    if (selected && !paintedOver) {
      const Rect label(textX, textY, textW, optionLineHeight);
      drawSelection(renderer, Rect(itemRectX, itemY, itemRectW, rowHeight), &label, 1);
    }
    // Unselected items: text is dark (invert=true means draw on white bg).
    // Selected on dark bg: text must be white (invert=false).
    // Selected on light bg: text stays dark (invert=true).
    const bool invertText = (selected && paintedOver) ? metrics.optionPopupSelectionLight : true;
    renderer.drawText(optionFontId, textX, textY, labelText, invertText, optionStyle);
  }
}

// Home in-progress list. Ported from the lector home's classic layout. Each book's
// Width of a chip that hugs "[NN%]" evenly, and where the text sits inside it.
//
// Padding by the same number of pixels on each side draws visibly lopsided, because
// getTextWidth measures the two ends differently. It returns an INK box, and its left
// edge is clamped to the pen (getTextBounds seeds minX with startX), so the opening
// bracket's own left side bearing — 4px of blank in Cozette 12 — is counted INSIDE the
// width, while the right edge stops exactly on the closing bracket's last lit pixel
// with no trailing blank at all. Equal padding therefore drew 4 + bearing on the left
// against a bare 4 on the right.
//
// So the fix is to pull the text left by that bearing: the ink then starts one gap in
// from the chip edge and ends one gap before the far edge. Read from the font rather
// than hardcoded, since the UI font is rebound per language.
void badgeChipMetrics(const GfxRenderer& renderer, const char* text, int* chipW, int* textDx) {
  constexpr int kGap = 4;  // blank between the chip edge and the bracket, both sides
  const int inkW = renderer.getTextWidth(UI_10_FONT_ID, text);

  int leftBearing = 0;
  const auto& fontMap = renderer.getFontMap();
  const auto it = fontMap.find(UI_10_FONT_ID);
  if (it != fontMap.end() && text[0] != '\0') {
    if (const EpdGlyph* first = it->second.getGlyph(static_cast<uint32_t>(text[0]))) {
      leftBearing = std::max(0, static_cast<int>(first->left));
    }
  }

  *textDx = kGap - leftBearing;
  *chipW = *textDx + inkW + kGap;
}

// full title + " by INITIALS" wraps across as many lines as it needs; a [NN%] badge
// with a black background sits inline on line 0 (it flips to a white chip on the
// selected/inverted row so it stays legible). "N more above/below" indicators show
// when the list scrolls. Returns the visible index range for the caller's scroll state.
ListVisibility BaseTheme::drawRecentBookList(GfxRenderer& renderer, Rect rect,
                                             const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                             int scrollOffset) const {
  constexpr int maxRowsCap = 30;
  const int count = std::min(static_cast<int>(recentBooks.size()), maxRowsCap);
  // Cap the measure loop at the store's own capacity rather than a smaller number:
  // the home list now grows into whatever the bottom-anchored menu leaves free, so a
  // lower cap would hide books that fit. Height still decides how many actually draw.
  constexpr int maxVisibleBooks = RecentBooksStore::MAX_RECENT_BOOKS;
  const int clampedOffset = std::max(0, std::min(scrollOffset, std::max(0, count - 1)));
  constexpr int rowGap = 4;
  const int rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int rowsTopInset = 10;
  constexpr int rowsBottomInset = 6;
  const int rowsTopMinY = rect.y + rowsTopInset;
  const int rowsBottomY = rect.y + rect.height - rowsBottomInset;
  const int rowsAvailableHeight = rowsBottomY - rowsTopMinY;
  const int availableRowW = std::max(1, rect.width - BaseMetrics::values.contentSidePadding * 2);
  constexpr int maxRowW = 520;
  const int rowW = std::min(availableRowW, maxRowW);
  const int rowX = rect.x + (rect.width - rowW) / 2;
  const int contentX = rowX + 10;
  const int contentW = std::max(1, rowW - 20);

  const int indicatorH = rowLineHeight + 8;
  const bool reserveIndicators = count > 1;
  const int effectiveTopY = rowsTopMinY + (reserveIndicators ? indicatorH : 0);
  const int effectiveBottomY = rowsBottomY - (reserveIndicators ? indicatorH : 0);
  const int contentHeight = effectiveBottomY - effectiveTopY;

  if (rowsAvailableHeight <= 0 || contentHeight <= 0 || count == 0) {
    return {0, 0, count};
  }

  struct BookEntry {
    int bookIdx;
    std::vector<std::string> lines;
    int height;
    int badgeW;       // 0 = no badge
    int badgeTextDx;  // where the text sits inside the chip (see badgeChipMetrics)
    std::string badgeText;
  };
  auto measureBook = [&](int idx) -> BookEntry {
    int badgeW = 0;
    int badgeTextDx = 0;
    std::string badgeText;
    if (recentBooks[idx].progressPercent >= 0) {
      char pctBuf[8];
      std::snprintf(pctBuf, sizeof(pctBuf), "[%d%%]", recentBooks[idx].progressPercent);
      badgeText = pctBuf;
      badgeChipMetrics(renderer, pctBuf, &badgeW, &badgeTextDx);
    }
    const int firstLineW = badgeW > 0 ? std::max(1, contentW - (badgeW + 6)) : contentW;
    // Initials by default; the full name when the user has asked for it in Settings.
    const std::string author = SETTINGS.authorDisplay == CrossPointSettings::AUTHOR_FULL_NAME
                                   ? recentBooks[idx].author
                                   : StringUtils::authorInitials(recentBooks[idx].author);
    const std::string rowText = author.empty() ? recentBooks[idx].title : (recentBooks[idx].title + " by " + author);
    // Every line gets the first line's width, because every line is drawn at the first
    // line's x: continuation lines used to run back to the left margin, under the [NN%]
    // chip, which left the block with a ragged left edge.
    auto lines = wrapText(renderer, rowText, firstLineW, firstLineW);
    const int h = static_cast<int>(lines.size()) * rowLineHeight + 6;
    return {idx, std::move(lines), h, badgeW, badgeTextDx, std::move(badgeText)};
  };

  auto buildVisibleEntries = [&](int startIdx) {
    std::vector<BookEntry> entries;
    int accumulated = 0;
    for (int i = startIdx; i < count && static_cast<int>(entries.size()) < maxVisibleBooks; i++) {
      auto entry = measureBook(i);
      const int needed = accumulated + (entries.empty() ? 0 : rowGap) + entry.height;
      if (needed > contentHeight) break;
      accumulated = needed;
      entries.push_back(std::move(entry));
    }
    return entries;
  };

  std::vector<BookEntry> visibleEntries = buildVisibleEntries(clampedOffset);

  // If the selected book is below the visible range, walk back from it so it lands at
  // the bottom with as many above as fit.
  if (selectorIndex >= 0 && selectorIndex < count && !visibleEntries.empty()) {
    const int lastVisibleIdx = visibleEntries.back().bookIdx;
    if (selectorIndex > lastVisibleIdx) {
      int totalH = measureBook(selectorIndex).height;
      int newOffset = selectorIndex;
      for (int i = selectorIndex - 1; i >= 0; i--) {
        const int h = measureBook(i).height;
        if (totalH + rowGap + h > contentHeight) break;
        totalH += rowGap + h;
        newOffset = i;
      }
      visibleEntries = buildVisibleEntries(newOffset);
    }
  }

  if (visibleEntries.empty() && clampedOffset < count) {
    visibleEntries.push_back(measureBook(clampedOffset));
  }

  const int firstVisible = visibleEntries.front().bookIdx;
  const int lastVisible = visibleEntries.back().bookIdx;
  const bool hasMoreAbove = firstVisible > 0;
  const bool hasMoreBelow = lastVisible < count - 1;

  int totalVisibleHeight = 0;
  for (size_t i = 0; i < visibleEntries.size(); i++) {
    totalVisibleHeight += visibleEntries[i].height;
    if (i > 0) totalVisibleHeight += rowGap;
  }
  // Centre the whole assembly — the "more above" chip, the rows, and the "more below"
  // chip — as one block in the band. Pinning each chip to its band edge instead left
  // the chip hard against the header while the rows floated in the middle, with the
  // unused indicator band showing up as a hole at the other end. The row-fitting maths
  // above still reserves both bands whenever the list can scroll, so the number of rows
  // on screen does not change as the chips come and go.
  const int aboveH = hasMoreAbove ? indicatorH : 0;
  const int belowH = hasMoreBelow ? indicatorH : 0;
  const int blockHeight = aboveH + totalVisibleHeight + belowH;
  const int blockTop = rowsTopMinY + std::max(0, (rowsAvailableHeight - blockHeight) / 2);
  int rowY = blockTop + aboveH;
  row_hit::Rows& recentHitRows = row_hit::lastRows();

  if (hasMoreAbove) {
    drawMoreIndicator(renderer, firstVisible, StrId::STR_MORE_ABOVE, rowX, rowW, blockTop, rowLineHeight);
  }

  for (const auto& entry : visibleEntries) {
    const bool selected = (selectorIndex == entry.bookIdx);
    // Solid paints the whole row and forces white text; the other styles mark it and
    // leave the text and the badge chip on their normal ground. Brackets hug the
    // title block, which starts after the [NN%] chip when the row carries one.
    bool inverted = false;
    if (selected) {
      const int titleX = contentX + (entry.badgeW > 0 ? entry.badgeW + 6 : 0);
      const Rect titleSpan(titleX, rowY + 3, rowX + rowW - titleX,
                           static_cast<int>(entry.lines.size()) * rowLineHeight);
      inverted = drawSelection(renderer, Rect(rowX, rowY, rowW, entry.height), &titleSpan, 1);
    }

    // [NN%] badge on line 0: an inverted chip that flips with row selection so it
    // stays legible on both grounds (black chip on an unselected row, white on the
    // selected/inverted row).
    int firstLineX = contentX;
    if (entry.badgeW > 0) {
      const int badgeH = rowLineHeight + 2;
      const int badgeY = rowY + 3 + (rowLineHeight - badgeH) / 2;
      renderer.fillRect(contentX, badgeY, entry.badgeW, badgeH, !inverted);
      renderer.drawText(UI_10_FONT_ID, contentX + entry.badgeTextDx, badgeY + (badgeH - rowLineHeight) / 2,
                        entry.badgeText.c_str(), inverted);
      firstLineX = contentX + entry.badgeW + 6;
    }

    int baselineY = rowY + 3;
    for (size_t li = 0; li < entry.lines.size(); li++) {
      // Wrapped lines line up under the first line's text, not under the chip.
      renderer.drawText(UI_10_FONT_ID, firstLineX, baselineY, entry.lines[li].c_str(), !inverted);
      baselineY += rowLineHeight;
    }

    recentHitRows.add(entry.bookIdx, rowX, rowY, rowW, entry.height);
    rowY += entry.height + rowGap;
  }

  if (hasMoreBelow) {
    // rowY has already advanced past the last row (plus its trailing gap), so the chip
    // sits directly under the rows rather than at the far bottom of the band.
    drawMoreIndicator(renderer, count - lastVisible - 1, StrId::STR_MORE_BELOW, rowX, rowW, rowY - rowGap + 2,
                      rowLineHeight);
  }

  return {firstVisible, lastVisible, count};
}
