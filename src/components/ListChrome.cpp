#include "ListChrome.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace {

int headerLineCount(const ListChrome& chrome) {
  int count = 0;
  for (const char* line : chrome.headerLines) {
    if (line != nullptr && line[0] != '\0') ++count;
  }
  return count;
}

bool present(const char* text) { return text != nullptr && text[0] != '\0'; }

list_chrome::Content contentFor(const ListChrome& chrome) {
  list_chrome::Content content;
  content.hasHeader = chrome.title != nullptr;
  content.hasSubHeader = present(chrome.subHeader);
  content.headerLines = headerLineCount(chrome);
  content.noteLines = present(chrome.note) ? 1 : 0;
  for (const char* line : chrome.footnotes) {
    if (present(line)) ++content.footnoteLines;
  }
  return content;
}

list_chrome::Metrics metricsFor(const GfxRenderer& renderer) {
  const auto& themeMetrics = UITheme::getInstance().getMetrics();
  list_chrome::Metrics metrics;
  metrics.screenWidth = renderer.getScreenWidth();
  metrics.screenHeight = renderer.getScreenHeight();
  metrics.topPadding = themeMetrics.topPadding;
  metrics.headerHeight = themeMetrics.headerHeight;
  metrics.subHeaderHeight = themeMetrics.tabBarHeight;
  metrics.lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  metrics.spacing = themeMetrics.verticalSpacing;
  metrics.hintsHeight = themeMetrics.buttonHintsHeight;
  return metrics;
}

Rect toRect(const list_chrome::Rect& rect) { return Rect{rect.x, rect.y, rect.width, rect.height}; }

}  // namespace

list_chrome::Bands listChromeBands(const GfxRenderer& renderer, const ListChrome& chrome) {
  return list_chrome::bandsFor(metricsFor(renderer), contentFor(chrome));
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
  const int lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  int y = bands.headerLines.y;
  for (const char* line : chrome.headerLines) {
    if (!present(line)) continue;
    // Centred, and drawn through the theme's help-text painter so a header
    // block and a note are the same face and the same truncation rule.
    GUI.drawHelpText(renderer, Rect{0, y, renderer.getScreenWidth(), lineHeight}, line);
    y += lineHeight;
  }
  if (present(chrome.note)) {
    GUI.drawHelpText(renderer, toRect(bands.note), chrome.note);
  }
}

void drawListChromeBottom(GfxRenderer& renderer, const MappedInputManager& mappedInput,
                          const ListChrome& chrome) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome);
  const int lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  int y = bands.footnote.y;
  for (const char* line : chrome.footnotes) {
    if (!present(line)) continue;
    GUI.drawHelpText(renderer, Rect{0, y, renderer.getScreenWidth(), lineHeight}, line);
    y += lineHeight;
  }
  const char* back = chrome.backHint != nullptr ? chrome.backHint : tr(STR_BACK);
  const char* confirm = chrome.confirmHint != nullptr ? chrome.confirmHint : tr(STR_SELECT);
  const char* third = chrome.thirdHint != nullptr ? chrome.thirdHint : tr(STR_DIR_UP);
  const char* fourth = chrome.fourthHint != nullptr ? chrome.fourthHint : tr(STR_DIR_DOWN);
  const auto labels = mappedInput.mapLabels(back, confirm, third, fourth);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
