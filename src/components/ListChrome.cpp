#include "ListChrome.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"

namespace {

bool present(const char* text) { return text != nullptr && text[0] != '\0'; }

// Every piece of chrome text wraps rather than being cut, so the bands are
// reserved from the same wrap the painter draws: one measure, both sides.
int wrappedLines(const GfxRenderer& renderer, const char* text) {
  return present(text) ? BaseTheme::helpTextLines(renderer, renderer.getScreenWidth(), text) : 0;
}

list_chrome::Content contentFor(const GfxRenderer& renderer, const ListChrome& chrome) {
  list_chrome::Content content;
  content.hasHeader = chrome.title != nullptr;
  content.hasSubHeader = present(chrome.subHeader);
  for (const char* line : chrome.headerLines) content.headerLines += wrappedLines(renderer, line);
  content.noteLines = wrappedLines(renderer, chrome.note);
  for (const char* line : chrome.footnotes) content.footnoteLines += wrappedLines(renderer, line);
  return content;
}

list_chrome::Metrics metricsFor(const GfxRenderer& renderer, const ListChrome& chrome) {
  const auto& themeMetrics = UITheme::getInstance().getMetrics();
  list_chrome::Metrics metrics;
  metrics.screenWidth = renderer.getScreenWidth();
  metrics.screenHeight = renderer.getScreenHeight();
  metrics.topPadding = themeMetrics.topPadding;
  // The band grows a line for every line the title wraps to.
  metrics.headerHeight = present(chrome.title) ? BaseTheme::headerHeightFor(renderer, metrics.screenWidth, chrome.title)
                                               : themeMetrics.headerHeight;
  metrics.subHeaderHeight = themeMetrics.tabBarHeight;
  metrics.lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  metrics.spacing = themeMetrics.verticalSpacing;
  metrics.hintsHeight = themeMetrics.buttonHintsHeight;
  return metrics;
}

Rect toRect(const list_chrome::Rect& rect) { return Rect{rect.x, rect.y, rect.width, rect.height}; }

// Draws one logical line wrapped, and returns the y under it.
int drawWrappedLine(const GfxRenderer& renderer, const int y, const char* line) {
  if (!present(line)) return y;
  const int lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  const int lines = BaseTheme::helpTextLines(renderer, renderer.getScreenWidth(), line);
  GUI.drawHelpText(renderer, Rect{0, y, renderer.getScreenWidth(), lineHeight * lines}, line);
  return y + lineHeight * lines;
}

}  // namespace

list_chrome::Bands listChromeBands(const GfxRenderer& renderer, const ListChrome& chrome) {
  return list_chrome::bandsFor(metricsFor(renderer, chrome), contentFor(renderer, chrome));
}

void drawListChromeTop(const GfxRenderer& renderer, const ListChrome& chrome) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome);
  if (chrome.title != nullptr) {
    GUI.drawHeader(renderer, toRect(bands.header), chrome.title[0] != '\0' ? chrome.title : nullptr,
                   chrome.headerRight);
  }
  if (present(chrome.subHeader)) {
    GUI.drawSubHeader(renderer, toRect(bands.subHeader), chrome.subHeader, chrome.subHeaderRight);
  }
  int y = bands.headerLines.y;
  for (const char* line : chrome.headerLines) y = drawWrappedLine(renderer, y, line);
  drawWrappedLine(renderer, bands.note.y, chrome.note);
}

void drawListChromeBottom(GfxRenderer& renderer, const MappedInputManager& mappedInput, const ListChrome& chrome) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome);
  int y = bands.footnote.y;
  for (const char* line : chrome.footnotes) y = drawWrappedLine(renderer, y, line);
  const char* back = chrome.backHint != nullptr ? chrome.backHint : tr(STR_BACK);
  const char* confirm = chrome.confirmHint != nullptr ? chrome.confirmHint : tr(STR_SELECT);
  const char* third = chrome.thirdHint != nullptr ? chrome.thirdHint : tr(STR_DIR_UP);
  const char* fourth = chrome.fourthHint != nullptr ? chrome.fourthHint : tr(STR_DIR_DOWN);
  const auto labels = mappedInput.mapLabels(back, confirm, third, fourth);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
