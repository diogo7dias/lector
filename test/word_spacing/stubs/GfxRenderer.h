#pragma once
#include <EpdFontFamily.h>
#include <Utf8.h>

#include <deque>
#include <string>

// Deterministic pixel metrics. Nonzero space kerning and style-dependent advances
// catch scaling the whole kerned gap or using the wrong word's font style.
class GfxRenderer {
 public:
  int getSpaceWidth(int, EpdFontFamily::Style style) const { return (style & EpdFontFamily::BOLD) ? 9 : 7; }
  int getKerning(int, uint32_t left, uint32_t right, EpdFontFamily::Style) const {
    return (left == 'a' && right == ' ') || (left == ' ' && right == 'b') ? -1 : 0;
  }
  int getSpaceAdvance(int font, uint32_t left, uint32_t right, EpdFontFamily::Style style) const {
    return getSpaceWidth(font, style) + getKerning(font, left, ' ', style) + getKerning(font, ' ', right, style);
  }
  int getTextAdvanceX(int font, const char* text, EpdFontFamily::Style style) const {
    int width = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
      const auto cp = utf8NextCodepoint(&p);
      width += cp == ' ' ? getSpaceWidth(font, style) : cp == 0xB7 ? 3 : (style & EpdFontFamily::BOLD) ? 11 : 10;
    }
    return width;
  }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, uint8_t) const {}
  void ensureSdCardFontReady(int, const char*, uint8_t) const {}
};
