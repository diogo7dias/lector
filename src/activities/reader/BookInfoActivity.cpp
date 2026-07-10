#include "BookInfoActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int SIDE_MARGIN = 20;      // left/right text inset for the description body
constexpr int COVER_MAX_FRACT = 40;  // cover band height as a percentage of the content area
}  // namespace

void BookInfoActivity::onEnter() {
  Activity::onEnter();
  // A cover is only shown when the reader managed to render a thumbnail for this book.
  hasCover = !coverBmpPath.empty() && Storage.exists(coverBmpPath.c_str());
  pageIndex = 0;
  requestUpdate();
}

void BookInfoActivity::onExit() { Activity::onExit(); }

void BookInfoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  // Up/Down page through the description (no wrap-around; clamps at the ends).
  buttonNavigator.onNext([this] {
    if (pageIndex < pageCount - 1) {
      pageIndex++;
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this] {
    if (pageIndex > 0) {
      pageIndex--;
      requestUpdate();
    }
  });
}

void BookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Battery cluster (top-right); title is drawn wrapped below so it can span lines.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight}, nullptr);

  // Title — wrapped, bold, centered (mirrors the reader menu header).
  const int batteryReserve = 12 + metrics.batteryWidth + renderer.getTextWidth(UI_10_FONT_ID, "100%") + 12;
  const int titleMaxWidth = screen.width - 2 * batteryReserve;
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), titleMaxWidth, 4, EpdFontFamily::BOLD);
  int y = screen.y + metrics.topPadding + 5;
  for (const auto& line : titleLines) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineHeight;
  }
  y += 2;

  const int subLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 2;

  if (!author.empty()) {
    const std::string byLine = std::string(tr(STR_BY_AUTHOR_PREFIX)) + author;
    const std::string truncated =
        renderer.truncatedText(UI_10_FONT_ID, byLine.c_str(), screen.width - 40, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, y, truncated.c_str());
    y += subLineHeight;
  }

  if (!language.empty()) {
    const std::string langLine = std::string(tr(STR_LANGUAGE)) + ": " + language;
    renderer.drawCenteredText(UI_10_FONT_ID, y, langLine.c_str());
    y += subLineHeight;
  }

  const int contentTop = y + metrics.verticalSpacing;
  const int contentBottom = screen.y + screen.height;

  // Cover band — draw the (1-bit) thumbnail centered at the top of the content area.
  int descTop = contentTop;
  if (hasCover) {
    HalFile f;
    if (Storage.openFileForRead("BINFO", coverBmpPath, f)) {
      Bitmap bm(f);
      if (bm.parseHeaders() == BmpReaderError::Ok && bm.getWidth() > 0 && bm.getHeight() > 0) {
        const int boxH = (contentBottom - contentTop) * COVER_MAX_FRACT / 100;
        const int boxW = screen.width * 3 / 5;
        // drawBitmap only downscales and keeps aspect; mirror that to get the real size.
        float s = std::min(static_cast<float>(boxW) / bm.getWidth(), static_cast<float>(boxH) / bm.getHeight());
        if (s > 1.0f) s = 1.0f;
        const int drawnW = static_cast<int>(bm.getWidth() * s);
        const int drawnH = static_cast<int>(bm.getHeight() * s);
        const int coverX = screen.x + (screen.width - drawnW) / 2;
        renderer.drawBitmap(bm, coverX, contentTop, drawnW, drawnH);
        renderer.drawRect(coverX, contentTop, drawnW, drawnH);
        descTop = contentTop + drawnH + metrics.verticalSpacing;
      }
    }
  }

  // Description — wrapped once per width, paged Up/Down.
  const int descMaxWidth = screen.width - 2 * SIDE_MARGIN;
  if (!description.empty()) {
    if (wrapWidth != descMaxWidth) {
      descLines = renderer.wrappedText(UI_12_FONT_ID, description.c_str(), descMaxWidth, 200, EpdFontFamily::REGULAR);
      wrapWidth = descMaxWidth;
    }

    const int bodyLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int descHeight = std::max(0, contentBottom - descTop);
    const int linesPerPage = std::max(1, descHeight / bodyLineHeight);
    const int totalLines = static_cast<int>(descLines.size());
    pageCount = std::max(1, (totalLines + linesPerPage - 1) / linesPerPage);
    if (pageIndex > pageCount - 1) pageIndex = pageCount - 1;

    const int firstLine = pageIndex * linesPerPage;
    const int lastLine = std::min(totalLines, firstLine + linesPerPage);
    int lineY = descTop + renderer.getFontAscenderSize(UI_12_FONT_ID);
    for (int i = firstLine; i < lastLine; i++) {
      renderer.drawText(UI_12_FONT_ID, screen.x + SIDE_MARGIN, lineY, descLines[i].c_str());
      lineY += bodyLineHeight;
    }
  } else {
    pageCount = 1;
    renderer.drawCenteredText(UI_10_FONT_ID, (descTop + contentBottom) / 2, tr(STR_NO_DESCRIPTION));
  }

  // Page indicator when the description spans multiple pages.
  const bool multiPage = pageCount > 1;
  if (multiPage) {
    const std::string pageLabel = std::to_string(pageIndex + 1) + " / " + std::to_string(pageCount);
    renderer.drawCenteredText(UI_10_FONT_ID, contentBottom - subLineHeight, pageLabel.c_str());
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", multiPage ? tr(STR_DIR_UP) : "", multiPage ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
