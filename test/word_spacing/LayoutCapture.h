#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "ParsedText.h"

namespace {
struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> x;
  std::vector<EpdFontFamily::Style> styles;
  std::vector<uint8_t> focus;
  std::vector<uint16_t> suffix;
  std::vector<uint16_t> dots;
};
std::vector<Line> lines;
const GfxRenderer renderer;
constexpr int FONT = 0;
constexpr const char* PROSE =
    "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";

void layout(const std::string& text, int percent, int width, CssTextAlign align = CssTextAlign::Left,
            uint8_t dots = GUIDE_DOTS_OFF, bool rtl = false, bool styled = false) {
  lines.clear();
  BlockStyle style;
  style.alignment = align;
  style.textAlignDefined = true;
  style.directionDefined = true;
  style.isRtl = rtl;
  ParsedText parsed(false, dots, style, 1, 0, percent);
  std::istringstream input(text);
  std::string word;
  int i = 0;
  while (input >> word) {
    const auto fontStyle = styled && ++i % 2 == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    // ChapterHtmlSlimParser::characterData emits NBSP as an attached space token.
    size_t start = 0;
    size_t nbsp;
    while ((nbsp = word.find("\xC2\xA0", start)) != std::string::npos) {
      parsed.addWord(word.substr(start, nbsp - start), fontStyle, start != 0);
      parsed.addWord(" ", fontStyle, true);
      start = nbsp + 2;
    }
    parsed.addWord(word.substr(start), fontStyle, start != 0);
  }
  parsed.layoutAndExtractLines(renderer, FONT, width, [](void*, std::unique_ptr<TextBlock>, uint32_t) {}, nullptr);
}

int rightEdge(const Line& line) {
  int edge = 0;
  for (size_t i = 0; i < line.words.size(); ++i)
    edge = std::max(edge, line.x[i] + renderer.getTextAdvanceX(FONT, line.words[i].c_str(), line.styles[i]));
  return edge;
}
}  // namespace
