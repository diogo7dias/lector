#pragma once
// Font metrics by id: every glyph advances fontId px, ascent is 2 * fontId, line height 3 * fontId.
#include <BidiUtils.h>
#include <EpdFontFamily.h>

#include <cstring>
#include <vector>

class GfxRenderer {
 public:
  struct Drawn {
    int font, x, y;
    bool black;
  };
  mutable std::vector<Drawn> drawn;
  int missingFont = -1;  // this id cannot be loaded

  bool ensureFontLoaded(const int id) const { return id != missingFont; }
  int getFontAscenderSize(const int id) const { return 2 * id; }
  int getLineHeight(const int id) const { return 3 * id; }
  int getTextAdvanceX(const int id, const char* text, EpdFontFamily::Style) const {
    return id * static_cast<int>(strlen(text));
  }
  int getTextWidth(const int id, const char* text, EpdFontFamily::Style, BidiUtils::BidiBaseDir) const {
    return getTextAdvanceX(id, text, EpdFontFamily::REGULAR);
  }
  bool getCodepointMetrics(int, uint32_t, EpdFontFamily::Style, int32_t&, int&) const { return false; }
  bool isFontCacheScanning() const { return false; }
  void drawText(const int font, const int x, const int y, const char*, const bool black, EpdFontFamily::Style,
                BidiUtils::BidiBaseDir) const {
    drawn.push_back({font, x, y, black});
  }
  void drawLine(int, int, int, int, int, bool) const {}
};
