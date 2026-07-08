#include "BaseTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/bookmark.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;
// Version footer, lifted clear of the bottom button-hints band (it sat too low
// at 738 and the up/down hint boxes overlapped it on the 792-tall X3 panel).
constexpr int subtitleY = 715;

// Collapse an author string to up to 4 leading-letter initials ("Ursula K. Le
// Guin" -> "UKLG"). Used by the home recent-books list rows. Ported from DX34.
std::string buildAuthorInitials(const std::string& author) {
  std::string initials;
  bool newWord = true;
  for (const char ch : author) {
    if (ch == ' ' || ch == '\t') {
      newWord = true;
      continue;
    }
    if (newWord) {
      if (ch >= 'a' && ch <= 'z') {
        initials.push_back(static_cast<char>(ch - ('a' - 'A')));
      } else {
        initials.push_back(ch);
      }
      if (initials.size() >= 4) {
        break;
      }
      newWord = false;
    }
  }
  return initials;
}

// Greedy word-wrap of input in the UI_10 font. Line 0 is wrapped to
// firstLineMaxWidth (leaving room for an inline [NN%] badge), later lines to
// restMaxWidth. An over-long single word is broken by character; a line that
// still won't fit is truncated. Ported from DX34, extended with the first-line
// indent.
std::vector<std::string> wrapText(const GfxRenderer& renderer, const std::string& input, int firstLineMaxWidth,
                                  int restMaxWidth) {
  std::vector<std::string> lines;
  if (input.empty()) {
    lines.push_back("");
    return lines;
  }

  size_t i = 0;
  while (i < input.size()) {
    while (i < input.size() && input[i] == ' ') {
      i++;
    }
    if (i >= input.size()) {
      break;
    }

    const int maxWidth = lines.empty() ? firstLineMaxWidth : restMaxWidth;
    std::string line;
    size_t lineEndPos = i;
    while (lineEndPos < input.size()) {
      size_t wordEnd = lineEndPos;
      while (wordEnd < input.size() && input[wordEnd] != ' ') {
        wordEnd++;
      }
      const std::string word = input.substr(lineEndPos, wordEnd - lineEndPos);
      const std::string candidate = line.empty() ? word : (line + " " + word);

      if (renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) <= maxWidth) {
        line = candidate;
        lineEndPos = wordEnd;
        while (lineEndPos < input.size() && input[lineEndPos] == ' ') {
          lineEndPos++;
        }
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

// Draw a centered "N more above/below" indicator badge. formatKey must be a
// translation key whose value contains a single "%d". Ported from DX34.
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

}  // namespace

// Draw the recent-books LIST on the home screen (Lector home). Ported from the
// DX34 Lector home's classic layout.
BookListVisibility BaseTheme::drawRecentBookList(GfxRenderer& renderer, Rect rect,
                                                 const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                                 int scrollOffset) const {
  // Hard ceiling on total rows; the caller (HomeActivity) decides how many
  // recent books to actually load for the list, so this is decoupled from the
  // single-cover homeRecentBooksCount metric.
  constexpr int maxRowsCap = 30;
  const int count = std::min(static_cast<int>(recentBooks.size()), maxRowsCap);
  constexpr int maxVisibleBooks = 8;
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

  // Indicator zone height (reserved when the list can scroll)
  const int indicatorH = rowLineHeight + 8;
  const bool reserveIndicators = count > 1;
  const int effectiveTopY = rowsTopMinY + (reserveIndicators ? indicatorH : 0);
  const int effectiveBottomY = rowsBottomY - (reserveIndicators ? indicatorH : 0);
  const int contentHeight = effectiveBottomY - effectiveTopY;

  if (rowsAvailableHeight <= 0 || contentHeight <= 0 || count == 0) {
    return {0, 0, count};
  }

  // A book row: the full title+author wrapped across as many lines as it needs.
  // When a [NN%] badge is present it sits inline on line 0, so line 0 wraps to a
  // narrower width (leaving room for the badge) and the rest hang at full width.
  struct BookEntry {
    int bookIdx;
    std::vector<std::string> lines;
    int height;
    int badgeW;  // 0 = no badge
    std::string badgeText;
  };
  auto measureBook = [&](int idx) -> BookEntry {
    int badgeW = 0;
    std::string badgeText;
    if (recentBooks[idx].progressPercent >= 0) {
      char pctBuf[8];
      std::snprintf(pctBuf, sizeof(pctBuf), "[%d%%]", recentBooks[idx].progressPercent);
      badgeText = pctBuf;
      badgeW = renderer.getTextWidth(UI_10_FONT_ID, pctBuf) + 8;  // chip padding
    }
    const int firstLineW = badgeW > 0 ? std::max(1, contentW - (badgeW + 6)) : contentW;
    const std::string initials = buildAuthorInitials(recentBooks[idx].author);
    const std::string rowText =
        initials.empty() ? recentBooks[idx].title : (recentBooks[idx].title + " by " + initials);
    auto lines = wrapText(renderer, rowText, firstLineW, contentW);
    const int h = static_cast<int>(lines.size()) * rowLineHeight + 6;
    return {idx, std::move(lines), h, badgeW, std::move(badgeText)};
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

  // If the selected book is below the visible range, walk back from it to find
  // the start offset that lands it at the bottom with as many above as fit.
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
  int rowY = effectiveTopY;
  if (totalVisibleHeight < contentHeight) {
    rowY += (contentHeight - totalVisibleHeight) / 2;
  }

  if (hasMoreAbove) {
    drawMoreIndicator(renderer, firstVisible, StrId::STR_MORE_ABOVE, rowX, rowW, rowsTopMinY, rowLineHeight);
  }

  for (const auto& entry : visibleEntries) {
    const bool selected = (selectorIndex == entry.bookIdx);
    if (selected) {
      renderer.fillRect(rowX, rowY, rowW, entry.height, true);  // whole row inverted
    }

    // [NN%] badge on line 0: an inverted chip that flips with row selection so
    // it stays legible on both grounds.
    int firstLineX = contentX;
    if (entry.badgeW > 0) {
      const int badgeH = rowLineHeight + 2;
      const int badgeY = rowY + 3 + (rowLineHeight - badgeH) / 2;
      renderer.fillRect(contentX, badgeY, entry.badgeW, badgeH, !selected);
      renderer.drawText(UI_10_FONT_ID, contentX + 4, badgeY + (badgeH - rowLineHeight) / 2, entry.badgeText.c_str(),
                        selected);
      firstLineX = contentX + entry.badgeW + 6;
    }

    int baselineY = rowY + 3;
    for (size_t li = 0; li < entry.lines.size(); li++) {
      const int lineX = (li == 0) ? firstLineX : contentX;
      renderer.drawText(UI_10_FONT_ID, lineX, baselineY, entry.lines[li].c_str(), !selected);
      baselineY += rowLineHeight;
    }

    rowY += entry.height + rowGap;
  }

  if (hasMoreBelow) {
    drawMoreIndicator(renderer, count - lastVisible - 1, StrId::STR_MORE_BELOW, rowX, rowW, effectiveBottomY + 2,
                      rowLineHeight);
  }

  return {firstVisible, lastVisible, count};
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
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;

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
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    // UI header battery %, sized to match the reader status bar text (Cozette 12).
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, percentageText.c_str());
    renderer.drawText(UI_10_FONT_ID, rect.x - textWidth - batteryPercentSpacing, rect.y, percentageText.c_str());
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

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // X3 has wider screen in portrait (528 vs 480), use more spacing
  constexpr int x4ButtonPositions[] = {25, 130, 245, 350};
  constexpr int x3ButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
      renderer.drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    }
  }

  renderer.setOrientation(orig_orientation);
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

int BaseTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  int rowHeight = (hasSubtitle) ? BaseMetrics::values.listWithSubtitleRowHeight : BaseMetrics::values.listRowHeight;
  return contentHeight / rowHeight;
}

void BaseTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? BaseMetrics::values.listWithSubtitleRowHeight : BaseMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;  // Offset from right edge

    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;  // Offset to avoid overlapping side button hints
    const int indicatorBottom = rect.y + rect.height - arrowSize;

    // Draw up arrow at top (^) - narrow point at top, wide base at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }

    // Draw down arrow at bottom (v) - wide base at top, narrow point at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  // Draw selection
  int contentWidth = rect.width - 5;
  if (selectedIndex >= 0) {
    renderer.fillRect(rect.x, rect.y + selectedIndex % pageItems * rowHeight - 2, rect.width, rowHeight);
  }
  constexpr int minValueGap = 10;

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;

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
    auto font = UI_10_FONT_ID;
    auto item = renderer.truncatedText(font, itemName.c_str(), rowTextWidth);
    renderer.drawText(font, rect.x + BaseMetrics::values.contentSidePadding, itemY, item.c_str(), i != selectedIndex);

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(font, item.c_str());
      const int lineH = renderer.getLineHeight(font);
      const int tx = rect.x + BaseMetrics::values.contentSidePadding;
      for (int py = itemY; py < itemY + lineH; py++)
        for (int px = tx; px < tx + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, rect.x + BaseMetrics::values.contentSidePadding, itemY + 22, subtitle.c_str(),
                          i != selectedIndex);
      }
    }

    if (!valueText.empty()) {
      const auto valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      int valueY = itemY;
      if (rowSubtitle != nullptr) {
        valueY = itemY + 10;
      }
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - BaseMetrics::values.contentSidePadding - valueTextWidth,
                        valueY, valueText.c_str(), i != selectedIndex);
    }
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  // Hide last battery draw. Wide enough to cover the larger UI_10 battery % text.
  constexpr int maxBatteryWidth = 100;
  renderer.fillRect(rect.x + rect.width - maxBatteryWidth, rect.y + 5, maxBatteryWidth,
                    BaseMetrics::values.batteryHeight + 10, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - BaseMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, BaseMetrics::values.batteryWidth, BaseMetrics::values.batteryHeight},
                   showBatteryPercentage);

  if (title) {
    int padding = rect.width - batteryX + BaseMetrics::values.batteryWidth;
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title,
                                                 rect.width - padding * 2 - BaseMetrics::values.contentSidePadding * 2,
                                                 EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, rect.y + 5, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(
        SMALL_FONT_ID, subtitle, rect.width - BaseMetrics::values.contentSidePadding * 2, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
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
      UI_12_FONT_ID, label, rect.width - BaseMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_12_FONT_ID, currentX, rect.y, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
}

void BaseTheme::drawTabBar(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  constexpr int underlineHeight = 2;  // Height of selection underline
  constexpr int underlineGap = 4;     // Gap between text and underline

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int textWidth =
        renderer.getTextWidth(UI_12_FONT_ID, tab.label, tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // Draw underline for selected tab
    if (tab.selected) {
      if (selected) {
        renderer.fillRect(currentX - 3, rect.y, textWidth + 6, lineHeight + underlineGap);
      } else {
        renderer.fillRect(currentX, rect.y + lineHeight + underlineGap, textWidth, underlineHeight);
      }
    }

    // Draw tab label
    renderer.drawText(UI_12_FONT_ID, currentX, rect.y, tab.label, !(tab.selected && selected),
                      tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }
}

// Draw the "Recent Book" cover card on the home screen
// TODO: Refactor method to make it cleaner, split into smaller methods
void BaseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const bool hasContinueReading = !recentBooks.empty();
  const bool bookSelected = hasContinueReading && selectorIndex == 0;

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
          if (bookSelected) {
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
        renderer.fillRect(bookX, bookY, bookWidth, bookHeight);
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
        renderer.fillPolygon(xPoints, yPoints, 5, !bookSelected);
      }
    }

    // If buffer was restored, draw selection indicators if needed
    if (bufferRestored && bookSelected && coverRendered) {
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
      renderer.fillRect(boxX, boxY, boxWidth, boxHeight, bookSelected);
      // Draw border around the box (inverted when selected: white border instead of black)
      renderer.drawRect(boxX, boxY, boxWidth, boxHeight, !bookSelected);
    }

    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, line.c_str(), !bookSelected);
      titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!truncatedAuthor.empty()) {
      titleYStart += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, truncatedAuthor.c_str(), !bookSelected);
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
      renderer.fillRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, bookSelected);
      renderer.drawRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, !bookSelected);
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, continueText, !bookSelected);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, tr(STR_CONTINUE_READING), !bookSelected);
    }
  } else {
    // No book to continue reading
    const int y =
        bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NO_OPEN_BOOK));
    renderer.drawCenteredText(UI_10_FONT_ID, y + renderer.getLineHeight(UI_12_FONT_ID), tr(STR_START_READING));
  }
}

void BaseTheme::drawRecentBookCoverflow(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        int centreIndex, bool coverSelected) const {
  const int count = static_cast<int>(recentBooks.size());
  if (count == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) / 2,
                              tr(STR_NO_OPEN_BOOK));
    return;
  }
  if (centreIndex < 0) centreIndex = 0;
  if (centreIndex >= count) centreIndex = count - 1;

  const int sidePad = BaseMetrics::values.contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int authorH = renderer.getLineHeight(SMALL_FONT_ID);

  // Reserve a band at the TOP for the [NN%] badge (drawn ABOVE the cover, outside
  // the art) and a band at the BOTTOM for the title + author. The cover fills what
  // is left; it may render smaller so the badge has its own space.
  const int badgeH = lineH + 4;
  const int topBandH = badgeH + 6;
  const int bottomBandH = lineH + authorH + 8;
  const int coverMaxH = std::max(40, rect.height - topBandH - bottomBandH);
  const int coverMaxW = std::max(20, rect.width - 2 * sidePad);
  const int coverBandY = rect.y + topBandH;
  const int peekVisible = 32;

  // Load a cover thumbnail's native dimensions. Returns false + empty when absent.
  auto loadDims = [&](const RecentBook& b, std::string& thumbOut, int& iw, int& ih) -> bool {
    iw = 0;
    ih = 0;
    if (b.coverBmpPath.empty()) return false;
    thumbOut = UITheme::getCoverThumbPath(b.coverBmpPath, BaseMetrics::values.homeCoverHeight);
    HalFile f;
    if (!Storage.openFileForRead("HOME", thumbOut, f)) return false;
    Bitmap bm(f);
    if (bm.parseHeaders() != BmpReaderError::Ok) return false;
    iw = bm.getWidth();
    ih = bm.getHeight();
    return iw > 0 && ih > 0;
  };

  // drawBitmap only DOWNSCALES (never upscales) and keeps aspect, so mirror that to
  // get the cover's ACTUAL rendered size — then the border/box we draw matches the
  // art exactly (no empty frame) and neighbours line up.
  auto drawnSize = [](int iw, int ih, int maxW, int maxH, int& ow, int& oh) {
    if (iw <= 0 || ih <= 0) {
      oh = maxH;
      ow = std::min(maxW, maxH * 2 / 3);
      return;
    }
    float s = std::min(static_cast<float>(maxW) / iw, static_cast<float>(maxH) / ih);
    if (s > 1.0f) s = 1.0f;
    ow = static_cast<int>(iw * s);
    oh = static_cast<int>(ih * s);
  };

  // Draw a cover into the exact box (leftX,topY,boxW,boxH). Border matches the art.
  auto drawCover = [&](const RecentBook& b, int leftX, int topY, int boxW, int boxH, bool selected) {
    std::string thumb;
    int iw = 0, ih = 0;
    if (loadDims(b, thumb, iw, ih)) {
      HalFile f;
      if (Storage.openFileForRead("HOME", thumb, f)) {
        Bitmap bm(f);
        if (bm.parseHeaders() == BmpReaderError::Ok) renderer.drawBitmap(bm, leftX, topY, boxW, boxH);
      }
    } else {
      renderer.fillRectDither(leftX, topY, boxW, boxH, Color::LightGray);
    }
    renderer.drawRect(leftX, topY, boxW, boxH);
    if (selected) {
      renderer.drawRect(leftX + 1, topY + 1, boxW - 2, boxH - 2);
      renderer.drawRect(leftX + 2, topY + 2, boxW - 4, boxH - 4);
    }
  };

  // Draw a neighbour peeking at a screen edge — its own real size, only ~peekVisible
  // px showing at the edge, vertically aligned with the centre band.
  auto drawPeek = [&](const RecentBook& b, bool leftSide) {
    std::string thumb;
    int iw = 0, ih = 0;
    loadDims(b, thumb, iw, ih);
    int w = 0, h = 0;
    drawnSize(iw, ih, coverMaxW, coverMaxH, w, h);
    const int topY = coverBandY + (coverMaxH - h) / 2;
    const int leftX = leftSide ? (rect.x + peekVisible - w) : (rect.x + rect.width - peekVisible);
    drawCover(b, leftX, topY, w, h, false);  // drawBitmap clips the off-screen part
  };

  const RecentBook& book = recentBooks[centreIndex];
  std::string cThumb;
  int cIw = 0, cIh = 0;
  loadDims(book, cThumb, cIw, cIh);
  int cW = 0, cH = 0;
  drawnSize(cIw, cIh, coverMaxW, coverMaxH, cW, cH);
  const int cX = rect.x + (rect.width - cW) / 2;
  const int cY = coverBandY + (coverMaxH - cH) / 2;

  // Peeks first (behind the centre).
  if (count > 1 && centreIndex > 0) drawPeek(recentBooks[centreIndex - 1], true);
  if (count > 1 && centreIndex < count - 1) drawPeek(recentBooks[centreIndex + 1], false);

  // Centre cover on top.
  drawCover(book, cX, cY, cW, cH, coverSelected);

  // [NN%] badge ABOVE the cover (top band, outside the art), centered.
  if (book.progressPercent >= 0) {
    const std::string pct = "[" + std::to_string(book.progressPercent) + "%]";
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, pct.c_str());
    const int bw = tw + 12;
    const int bx = rect.x + (rect.width - bw) / 2;
    const int by = rect.y + (topBandH - badgeH) / 2;
    renderer.fillRect(bx, by, bw, badgeH, true);
    renderer.drawText(UI_10_FONT_ID, bx + (bw - tw) / 2, by + 2, pct.c_str(), false);
  }

  // Title + author below the cover band.
  const int textMaxW = rect.width - 2 * sidePad;
  int ty = coverBandY + coverMaxH + 4;
  {
    const std::string t = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), textMaxW, EpdFontFamily::BOLD);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, t.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, rect.x + (rect.width - tw) / 2, ty, t.c_str(), true, EpdFontFamily::BOLD);
    ty += lineH + 2;
  }
  if (!book.author.empty()) {
    const std::string a = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), textMaxW);
    const int aw = renderer.getTextWidth(SMALL_FONT_ID, a.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + (rect.width - aw) / 2, ty, a.c_str(), true);
  }
}

void BaseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = BaseMetrics::values.verticalSpacing + rect.y +
                      static_cast<int>(i) * (BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing);

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    } else {
      renderer.drawRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY =
        tileY + (BaseMetrics::values.menuRowHeight - lineHeight) / 2;  // vertically centered assuming y is top of text
    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, selectedIndex != i);
  }
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  // Lector popup = a full-width black banner with white centered text, matching
  // the book-open "Opening..." banner. Height keeps the popup's top+bottom
  // margin so fillPopupProgress still has room to draw a progress bar in the
  // lower margin. Vertically centered on screen.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginY = metrics.popupMarginY;
  const int pageWidth = renderer.getScreenWidth();
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int h = textHeight + marginY * 2;
  const int y = (renderer.getScreenHeight() - h) / 2;

  renderer.fillRect(0, y, pageWidth, h, true);  // black banner
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message);
  const int textX = (pageWidth - textWidth) / 2;
  const int textY = y + (h - textHeight) / 2;
  // Smear the white text +1px ("dark"/paperback look) so it reads heavier on the
  // black banner, then restore so nothing else is affected.
  renderer.setPaperbackLook(true);
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, false);  // white text
  renderer.setPaperbackLook(false);
  renderer.displayBuffer();
  return Rect{0, y, pageWidth, h};
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int barHeight = metrics.popupProgressBarHeight;
  const int barWidth =
      std::max(0, layout.width - metrics.popupMarginX * 2);  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - metrics.popupMarginY / 2 - barHeight / 2 - 1;
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
  if (!SETTINGS.sbEnabled) return;

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
  // Separator between co-anchored items: a drawn vertical bar with equal gaps on
  // each side. A " | " string looked lopsided because the '|' glyph sits
  // off-centre in its monospace cell (wide gap before, tight after).
  const int sepGap = 4;   // even gap each side of the bar
  const int sepBarW = 1;  // bar thickness
  const int sepW = sepGap + sepBarW + sepGap;
  const bool showBattery = SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_NEVER;

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

  // --- Progress bars (full width, flush to the edge) ------------------------
  const int barPx = statusBarThicknessPx(SETTINGS.sbBarThickness);
  const int barLeft = ml;
  const int barMaxW = screenW - ml - mr;
  auto clampPct = [](int p) { return p < 0 ? 0 : (p > 100 ? 100 : p); };
  auto drawEdgeBar = [&](int y, int pct) {
    const int w = barMaxW * clampPct(pct) / 100;
    if (w > 0) renderer.fillRect(barLeft, y, w, barPx, true);
  };

  // Top edge: book bar then chapter bar flush to the top; text band below them.
  int topStack = mt;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_TOP) {
    drawEdgeBar(topStack, data.bookPercent);
    topStack += barPx;
  }
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_TOP && data.hasChapters) {
    drawEdgeBar(topStack, data.chapterPercent);
    topStack += barPx;
  }
  const int topTextY = topStack + 2;

  // Bottom edge: bars flush to the bottom; text band above them.
  int bottomStack = screenH - mb;
  if (SETTINGS.sbBookBar == CrossPointSettings::SB_EDGE_BOTTOM) {
    bottomStack -= barPx;
    drawEdgeBar(bottomStack, data.bookPercent);
  }
  if (SETTINGS.sbChapterBar == CrossPointSettings::SB_EDGE_BOTTOM && data.hasChapters) {
    bottomStack -= barPx;
    drawEdgeBar(bottomStack, data.chapterPercent);
  }
  const int bottomTextY = bottomStack - lineH - 2;

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

void BaseTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, const bool isSelected,
                                const char* secondaryLabel, const KeyboardKeyType keyType,
                                const bool inactiveSelection) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int cr = metrics.keyboardKeyCornerRadius;
  const bool isSpecialKey = keyType == KeyboardKeyType::Shift || keyType == KeyboardKeyType::Mode ||
                            keyType == KeyboardKeyType::Del || keyType == KeyboardKeyType::Space ||
                            keyType == KeyboardKeyType::Ok || keyType == KeyboardKeyType::Disabled;

  if (isSelected) {
    if (inactiveSelection) {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::LightGray);
      } else {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);
      }
    } else if (keyType == KeyboardKeyType::Disabled) {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::LightGray);
      } else {
        renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
      }
    } else {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::Black);
      } else {
        renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
      }
    }
  } else {
    if (metrics.keyboardFillUnselected) {
      if (keyType == KeyboardKeyType::Disabled) {
        if (cr > 0) {
          renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::LightGray);
        } else {
          renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
        }
      } else {
        if (cr > 0) {
          renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::White);
        } else {
          renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
        }
      }
    }

    const bool shouldDrawOutline =
        (metrics.keyboardDrawSpecialOutlineWhenUnselected && isSpecialKey) || metrics.keyboardOutlineAllUnselected;
    if (shouldDrawOutline) {
      if (cr > 0) {
        renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, cr, true);
      } else {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
      }
    }
  }

  const bool invert = isSelected && !inactiveSelection;

  if (keyType == KeyboardKeyType::Space) {
    const int lineHalfWidth = rect.width * 3 / 10;
    const int centerX = rect.x + rect.width / 2;
    const int lineY = rect.y + rect.height / 2 + 3;
    renderer.drawLine(centerX - lineHalfWidth, lineY, centerX + lineHalfWidth, lineY, 3, !invert);
    return;
  }

  if (keyType == KeyboardKeyType::Del) {
    const int centerX = rect.x + rect.width / 2;
    const int centerY = rect.y + rect.height / 2;
    const int arrowLen = rect.width / 4;
    const int arrowHead = std::max(metrics.keyboardMinArrowHeadSize, arrowLen / 2);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX + arrowLen / 2, centerY, 3, !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY - arrowHead, 3,
                      !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY + arrowHead, 3,
                      !invert);
    return;
  }

  if (label == nullptr || label[0] == '\0') {
    return;
  }

  const bool hasSecondary = secondaryLabel != nullptr && secondaryLabel[0] != '\0';
  const int itemWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = rect.x + (rect.width - itemWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;

  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !invert);

  if (hasSecondary) {
    const int secWidth = renderer.getTextWidth(SMALL_FONT_ID, secondaryLabel);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - secWidth - metrics.keyboardSecondaryLabelRightPadding,
                      rect.y + metrics.keyboardSecondaryLabelTopPadding, secondaryLabel, !invert);
  }
}
