#include "ListChrome.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <cmath>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "UiFont.h"
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
// placed it: a CSS line box of `box` pixels with the font's own line centred in it and
// the text on that line's baseline. Sizes are the faces' own; the chapter and progress
// lines are set at the author's size.
namespace title_page {

constexpr float TOP = 34;          // panel top to the title's line box
constexpr float SIDE = 30;         // text inset from both edges
constexpr float BOTTOM = 18;       // progress line to the first row
constexpr float LINE_GAP = 4;      // above the author and above the progress line
constexpr float RULE_MARGIN = 14;  // above and below the rule
constexpr int RULE_W = 64;
constexpr int RULE_H = 2;

struct Face {
  int fontId;
  float box;       // CSS line box
  float tracking;  // CSS letter-spacing
};
constexpr Face TITLE{LITERATA_UI_25_FONT_ID, 31.25f, 5.5f};
constexpr Face AUTHOR{LITERATA_UI_19_IT_FONT_ID, 23.75f, 0};
constexpr Face CHAPTER{LITERATA_UI_19_SC_FONT_ID, 23.75f, 2.66f};
constexpr Face PROGRESS{LITERATA_UI_19_IT_FONT_ID, 23.75f, 0};
constexpr Face FOOTNOTE{LITERATA_UI_19_IT_FONT_ID, 23.75f, 0};
constexpr int FOOTNOTE_LINE = 24;  // the band reserves whole pixels
constexpr int FOOT = 18;           // last footnote line to the panel's foot

// Arabic and Hebrew UIs draw it all in their UI font, which Literata cannot stand in for.
int fontOf(const Face& face) { return uiLanguageNeedsUbuntu() ? UI_10_FONT_ID : face.fontId; }

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

// One codepoint's advance. A space has no glyph to measure, so it takes the font's space.
int glyphWidth(const GfxRenderer& renderer, const int fontId, const uint32_t cp, const char* glyph) {
  return cp == ' ' ? renderer.getSpaceWidth(fontId) : renderer.getTextWidth(fontId, glyph);
}

// A line's width with the face's letter-spacing after every character, as CSS adds it.
// Untracked faces measure the whole line, kerning included.
float lineWidth(const GfxRenderer& renderer, const Face& face, const std::string& text) {
  const int fontId = fontOf(face);
  if (face.tracking == 0) return static_cast<float>(renderer.getTextWidth(fontId, text.c_str()));
  float width = 0;
  char glyph[5];
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    *utf8EncodeCodepoint(cp, glyph) = '\0';
    width += static_cast<float>(glyphWidth(renderer, fontId, cp, glyph)) + face.tracking;
  }
  return width;
}

// Greedy word wrap, never cutting: a word wider than the line gets a line of its own.
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
    if (!line.empty() && lineWidth(renderer, face, candidate) > maxW) {
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
  const int fontId = fontOf(face);
  const int width = renderer.getScreenWidth();
  for (const std::string& line : wrap(renderer, face, text, static_cast<float>(width) - SIDE * 2)) {
    if (draw) {
      const int top = static_cast<int>(std::lround(y + (face.box - renderer.getLineHeight(fontId)) / 2));
      float x = (static_cast<float>(width) - lineWidth(renderer, face, line)) / 2;
      if (face.tracking == 0) {
        renderer.drawText(fontId, static_cast<int>(std::lround(x)), top, line.c_str());
      } else {
        char glyph[5];
        const auto* p = reinterpret_cast<const unsigned char*>(line.c_str());
        while (const uint32_t cp = utf8NextCodepoint(&p)) {
          *utf8EncodeCodepoint(cp, glyph) = '\0';
          if (cp != ' ') renderer.drawText(fontId, static_cast<int>(std::lround(x)), top, glyph);
          x += static_cast<float>(glyphWidth(renderer, fontId, cp, glyph)) + face.tracking;
        }
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

int footnoteLines(const GfxRenderer& renderer, const char* text) {
  if (!present(text)) return 0;
  return static_cast<int>(
      wrap(renderer, FOOTNOTE, text, static_cast<float>(renderer.getScreenWidth()) - SIDE * 2).size());
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
  for (const char* line : chrome.footnotes) {
    content.footnoteLines += chrome.contents ? title_page::footnoteLines(renderer, line) : wrappedLines(renderer, line);
  }
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
  if (chrome.contents) {
    // Only footnotes use the line height here: the rest went into the title page.
    metrics.lineHeight = title_page::FOOTNOTE_LINE;
    if (!chrome.hints && present(chrome.footnotes[0])) metrics.hintsHeight = title_page::FOOT;
  }
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

void toContentsLook(ListChrome& chrome, const bool keepHints) {
  chrome.contents = true;
  chrome.hints = chrome.hints && keepHints;
  if (chrome.titlePage.title == nullptr && chrome.title != nullptr) {
    chrome.titlePage.title = chrome.title;
    for (const char* line : {chrome.subHeader, chrome.headerRight, chrome.note}) {
      if (present(line)) {
        chrome.titlePage.author = line;
        break;
      }
    }
  }
  chrome.title = nullptr;
  chrome.headerRight = nullptr;
  chrome.footerRight = nullptr;
  chrome.subHeader = nullptr;
  chrome.subHeaderRight = nullptr;
  chrome.headerLines = {};
  chrome.note = nullptr;
}

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
  if (chrome.contents) {
    auto y = static_cast<float>(bands.footnote.y);
    for (const char* line : chrome.footnotes) {
      if (present(line)) y = title_page::block(renderer, title_page::FOOTNOTE, line, y, true);
    }
  } else {
    int y = bands.footnote.y;
    for (const char* line : chrome.footnotes) y = drawWrappedLine(renderer, y, line);
  }
  if (!chrome.hints) return;
  const char* back = chrome.backHint != nullptr ? chrome.backHint : tr(STR_BACK);
  const char* confirm = chrome.confirmHint != nullptr ? chrome.confirmHint : tr(STR_SELECT);
  const char* third = chrome.thirdHint != nullptr ? chrome.thirdHint : tr(STR_DIR_UP);
  const char* fourth = chrome.fourthHint != nullptr ? chrome.fourthHint : tr(STR_DIR_DOWN);
  const auto labels = mappedInput.mapLabels(back, confirm, third, fourth);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
