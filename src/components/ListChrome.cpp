#include "ListChrome.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <cmath>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

namespace {

bool present(const char* text) { return text != nullptr && text[0] != '\0'; }

// Every piece of chrome text wraps rather than being cut, so the bands are
// reserved from the same wrap the painter draws: one measure, both sides.
int wrappedLines(const GfxRenderer& renderer, const char* text) {
  return present(text) ? BaseTheme::helpTextLines(renderer, renderer.getScreenWidth(), text) : 0;
}

// The contents look's title page. Every line is placed the way the mockup's browser
// placed it: a CSS line box of `box` pixels with the baseline half-leading into it,
// from Literata's hhea metrics (ascent 1177, descent 308 per 1000 units), so the
// firmware lands each baseline on the same pixel row. Sizes are the faces' own.
namespace title_page {

constexpr float ASCENT = 1.177f;
constexpr float DESCENT = 0.308f;
constexpr float TOP = 34;          // panel top to the title's line box
constexpr float SIDE = 30;         // text inset from both edges
constexpr float BOTTOM = 18;       // progress line to the first row
constexpr float LINE_GAP = 4;      // above the author and above the progress line
constexpr float RULE_MARGIN = 14;  // above and below the rule
constexpr int RULE_W = 64;
constexpr int RULE_H = 2;

struct Face {
  int fontId;
  float px;
  float box;       // CSS line box
  float tracking;  // CSS letter-spacing
};
constexpr Face TITLE{LITERATA_UI_25_FONT_ID, 25, 31.25f, 5.5f};
constexpr Face AUTHOR{LITERATA_UI_19_IT_FONT_ID, 19, 23.75f, 0};
constexpr Face CHAPTER{LITERATA_UI_15_SC_FONT_ID, 15, 18.75f, 2.1f};
constexpr Face PROGRESS{LITERATA_UI_15_IT_FONT_ID, 15, 18.75f, 0};

float baselineIn(const Face& face) { return (face.box - (ASCENT + DESCENT) * face.px) / 2 + ASCENT * face.px; }

// ASCII case changes only: the title is set in capitals and the chapter in small
// capitals (lower case into the small-caps face); other scripts pass through.
std::string recase(const char* text, const bool upper) {
  std::string out(text);
  for (char& c : out) {
    if (upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (!upper && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

float trackedWidth(const GfxRenderer& renderer, const Face& face, const std::string& text) {
  float width = 0;
  char glyph[5];
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    *utf8EncodeCodepoint(cp, glyph) = '\0';
    width += static_cast<float>(renderer.getTextWidth(face.fontId, glyph)) + face.tracking;
  }
  return width;
}

// Greedy word wrap on the tracked width, never cutting: a word wider than the line
// gets a line of its own.
std::vector<std::string> wrap(const GfxRenderer& renderer, const Face& face, const std::string& text,
                              const float maxW) {
  std::vector<std::string> lines;
  std::string line;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(' ', start);
    if (end == std::string::npos) end = text.size();
    const std::string word = text.substr(start, end - start);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && trackedWidth(renderer, face, candidate) > maxW) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// Draws (or only measures) one block of centred lines from line-box top y; returns
// the y under it.
float block(const GfxRenderer& renderer, const Face& face, const std::string& text, float y, const bool draw) {
  const int width = renderer.getScreenWidth();
  for (const std::string& line : wrap(renderer, face, text, static_cast<float>(width) - SIDE * 2)) {
    if (draw) {
      const int top = static_cast<int>(std::lround(y + baselineIn(face))) - renderer.getFontAscenderSize(face.fontId);
      float x = (static_cast<float>(width) - trackedWidth(renderer, face, line)) / 2;
      char glyph[5];
      const auto* p = reinterpret_cast<const unsigned char*>(line.c_str());
      while (const uint32_t cp = utf8NextCodepoint(&p)) {
        *utf8EncodeCodepoint(cp, glyph) = '\0';
        renderer.drawText(face.fontId, static_cast<int>(std::lround(x)), top, glyph);
        x += static_cast<float>(renderer.getTextWidth(face.fontId, glyph)) + face.tracking;
      }
    }
    y += face.box;
  }
  return y;
}

// The whole page; returns its height, which is what the bands reserve.
int layout(const GfxRenderer& renderer, const ListChrome::TitlePage& page, const bool draw) {
  float y = TOP;
  if (present(page.title)) y = block(renderer, TITLE, recase(page.title, true), y, draw);
  if (present(page.author)) y = block(renderer, AUTHOR, page.author, y + LINE_GAP, draw);
  y += RULE_MARGIN;
  if (draw) {
    renderer.fillRect((renderer.getScreenWidth() - RULE_W) / 2, static_cast<int>(std::lround(y)), RULE_W, RULE_H);
  }
  y += RULE_H + RULE_MARGIN;
  if (present(page.chapter)) y = block(renderer, CHAPTER, recase(page.chapter, false), y, draw);
  if (present(page.progress)) y = block(renderer, PROGRESS, page.progress, y + LINE_GAP, draw);
  return static_cast<int>(std::lround(y + BOTTOM));
}

}  // namespace title_page

list_chrome::Content contentFor(const GfxRenderer& renderer, const ListChrome& chrome) {
  list_chrome::Content content;
  content.hasHeader = chrome.title != nullptr;
  content.hasSubHeader = present(chrome.subHeader);
  if (chrome.titlePage.title != nullptr)
    content.titlePageHeight = title_page::layout(renderer, chrome.titlePage, false);
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
  metrics.headerHeight = present(chrome.title) ? BaseTheme::headerHeightFor(renderer, metrics.screenWidth, chrome.title,
                                                                            chrome.headerRight)
                                               : themeMetrics.headerHeight;
  metrics.subHeaderHeight = themeMetrics.tabBarHeight;
  metrics.lineHeight = renderer.getLineHeight(uiScaleSpec().smallFontId);
  metrics.spacing = themeMetrics.verticalSpacing;
  metrics.hintsHeight = chrome.hints ? themeMetrics.buttonHintsHeight : 0;
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
    GUI.drawHeader(renderer, toRect(bands.header), chrome.title[0] != '\0' ? chrome.title : nullptr, chrome.footerRight,
                   chrome.headerRight);
  }
  if (present(chrome.subHeader)) {
    GUI.drawSubHeader(renderer, toRect(bands.subHeader), chrome.subHeader, chrome.subHeaderRight);
  }
  if (chrome.titlePage.title != nullptr) title_page::layout(renderer, chrome.titlePage, true);
  int y = bands.headerLines.y;
  for (const char* line : chrome.headerLines) y = drawWrappedLine(renderer, y, line);
  drawWrappedLine(renderer, bands.note.y, chrome.note);
}

void drawListChromeBottom(GfxRenderer& renderer, const MappedInputManager& mappedInput, const ListChrome& chrome) {
  const list_chrome::Bands bands = listChromeBands(renderer, chrome);
  int y = bands.footnote.y;
  for (const char* line : chrome.footnotes) y = drawWrappedLine(renderer, y, line);
  if (!chrome.hints) return;
  const char* back = chrome.backHint != nullptr ? chrome.backHint : tr(STR_BACK);
  const char* confirm = chrome.confirmHint != nullptr ? chrome.confirmHint : tr(STR_SELECT);
  const char* third = chrome.thirdHint != nullptr ? chrome.thirdHint : tr(STR_DIR_UP);
  const char* fourth = chrome.fourthHint != nullptr ? chrome.fourthHint : tr(STR_DIR_DOWN);
  const auto labels = mappedInput.mapLabels(back, confirm, third, fourth);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
