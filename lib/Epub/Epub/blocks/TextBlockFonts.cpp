// Per-word fonts (CSS font-size spans, small caps): one font table per line, a slot per word.
#include <GfxRenderer.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "TextBlock.h"

bool TextBlock::setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]) {
  wordFontData = makeUniqueNoThrow<uint8_t[]>(sizeof(fonts) + numWords);
  if (!wordFontData) return false;
  memcpy(wordFontData.get(), fonts, sizeof(fonts));
  memcpy(wordFontData.get() + sizeof(fonts), slots, numWords);
  return true;
}

int TextBlock::renderFontId(const GfxRenderer& renderer, const int fontId, const int sectionFontId) {
  return fontId == sectionFontId || renderer.ensureFontLoaded(fontId) ? fontId : sectionFontId;
}

int TextBlock::wordFontId(const uint16_t i, const int sectionFontId) const {
  if (!wordFontData) return blockFontId(sectionFontId);
  const uint8_t slot = wordFontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & ~SMALL_CAPS_SLOT_FLAG;
  if (slot == 0) return blockFontId(sectionFontId);
  int32_t font = 0;
  memcpy(&font, wordFontData.get() + sizeof(int32_t) * (slot - 1), sizeof(font));
  return font;
}

int TextBlock::lineAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  int ascent = renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
  if (!wordFontData) return ascent;
  for (uint8_t slot = 0; slot < MAX_WORD_FONTS; ++slot) {
    int32_t font = 0;
    memcpy(&font, wordFontData.get() + sizeof(int32_t) * slot, sizeof(font));
    if (font != 0) ascent = std::max(ascent, renderer.getFontAscenderSize(renderFontId(renderer, font, sectionFontId)));
  }
  return ascent;
}

int TextBlock::wordYOffset(const GfxRenderer& renderer, const int sectionFontId, const uint16_t i) const {
  if (!wordFontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId));
}

int TextBlock::extraAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  if (!wordFontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
}
