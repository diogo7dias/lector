// A font without a Unicode space must not draw the replacement box in its place.

#include <gtest/gtest.h>

#include <cstring>

#include "EpdFont.h"
#include "EpdFontData.h"

namespace {

// Glyph order follows the intervals: space, U+2009 (second font only), U+FFFD.
const EpdGlyph kGlyphs[] = {
    {0, 0, 5 << 4, 0, 0, 0, 0},    // ' '
    {0, 0, 2 << 4, 0, 0, 0, 0},    // U+2009, a native thin space
    {8, 12, 9 << 4, 0, 12, 0, 0},  // U+FFFD
};
const EpdUnicodeInterval kPlain[] = {{0x20, 0x20, 0}, {0xFFFD, 0xFFFD, 2}};
const EpdUnicodeInterval kWithThin[] = {{0x20, 0x20, 0}, {0x2009, 0x2009, 1}, {0xFFFD, 0xFFFD, 2}};

template <size_t N>
EpdFont makeFont(EpdFontData& data, const EpdUnicodeInterval (&intervals)[N]) {
  std::memset(&data, 0, sizeof(data));
  data.glyph = kGlyphs;
  data.intervals = intervals;
  data.intervalCount = N;
  return EpdFont(&data);
}

}  // namespace

TEST(SpaceFallback, MissingSpacesUseTheSpaceGlyph) {
  EpdFontData data;
  const EpdFont font = makeFont(data, kPlain);
  for (const uint32_t cp : {0x00A0u, 0x2002u, 0x2009u, 0x200Au, 0x202Fu, 0x205Fu, 0x3000u}) {
    EXPECT_EQ(font.getGlyph(cp), &kGlyphs[0]) << std::hex << cp;
  }
}

TEST(SpaceFallback, MissingZeroWidthCharactersDrawNothing) {
  EpdFontData data;
  const EpdFont font = makeFont(data, kPlain);
  for (const uint32_t cp : {0x200Bu, 0x200Cu, 0x200Du, 0x2060u, 0xFEFFu}) {
    EXPECT_EQ(font.getGlyph(cp), nullptr) << std::hex << cp;
  }
}

TEST(SpaceFallback, NativeGlyphAndOtherMissesAreUnchanged) {
  EpdFontData data;
  const EpdFont thin = makeFont(data, kWithThin);
  EXPECT_EQ(thin.getGlyph(0x2009), &kGlyphs[1]);
  EXPECT_EQ(thin.getGlyph(0x4E00), &kGlyphs[2]);
}
