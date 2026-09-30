#pragma once

#include <GfxRenderer.h>

#include <algorithm>

// The contents look's selection on the page: the word in bold over a thick underline,
// where the old look inverted it into a black box. Shared by word select and quote select.
namespace word_emphasis {

constexpr int UNDERLINE = 3;  // px thick
constexpr int DROP = 3;       // px from the baseline to the underline's top

inline EpdFontFamily::Style bold(const EpdFontFamily::Style style) {
  return static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
}

// How wide the emphasised word draws: bold runs wider than the regular it replaces.
inline int width(const GfxRenderer& renderer, const int fontId, const char* text, const EpdFontFamily::Style style,
                 const int regularWidth) {
  return std::max(regularWidth, renderer.getTextWidth(fontId, text, bold(style)));
}

// Clears the words' line box, so the bold does not land on the regular underneath.
inline void clear(const GfxRenderer& renderer, const int x, const int y, const int w, const int lineHeight) {
  renderer.fillRect(x - 1, y - 1, w + 2, lineHeight + 2, false);
}

inline void word(const GfxRenderer& renderer, const int fontId, const int x, const int y, const char* text,
                 const EpdFontFamily::Style style) {
  renderer.drawText(fontId, x, y, text, true, bold(style));
}

inline void underline(const GfxRenderer& renderer, const int fontId, const int x0, const int x1, const int y) {
  renderer.fillRect(x0, y + renderer.getFontAscenderSize(fontId) + DROP, x1 - x0, UNDERLINE, true);
}

}  // namespace word_emphasis
