// Host tests for makeRenderSpec(), the one builder behind the reader's section cache key
// and the Text Settings preview key.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "ReaderPrefs.h"

namespace {

// Reader Settings rows that do not reach the spec: the font is resolved by the caller,
// the margins become the viewport, and the rest are draw-only.
const std::set<std::string> NOT_IN_SPEC = {
    "fontFamily",     "fontPointSize",  "screenMargin",     "screenMarginTop", "screenMarginBottom",
    "marginLinkMode", "dynamicMargins", "textAntiAliasing", "textContrast",
};

// Guide dots on, so Hidden Dots has something to change.
ReaderPrefs baseLook() {
  ReaderPrefs p;
  p.guideDotsEnabled = 1;
  return p;
}

}  // namespace

TEST(RenderSpec, ClampsLineSpacingToTheRowRange) {
  ReaderPrefs p;
  p.lineSpacingPercent = 0;
  EXPECT_FLOAT_EQ(makeRenderSpec(p, 1, 480, 800).lineCompression, reader_defaults::MIN_LINE_SPACING_PERCENT / 100.0f);
  p.lineSpacingPercent = 255;
  EXPECT_FLOAT_EQ(makeRenderSpec(p, 1, 480, 800).lineCompression, reader_defaults::MAX_LINE_SPACING_PERCENT / 100.0f);
}

TEST(RenderSpec, ClampsWordSpacingToTheRowRange) {
  ReaderPrefs p;
  p.wordSpacing = 0;
  EXPECT_EQ(makeRenderSpec(p, 1, 480, 800).wordSpacing, reader_defaults::MIN_WORD_SPACING);
  p.wordSpacing = 255;
  EXPECT_EQ(makeRenderSpec(p, 1, 480, 800).wordSpacing, reader_defaults::MAX_WORD_SPACING);
}

TEST(RenderSpec, NormalisesSwitches) {
  ReaderPrefs p;
  p.extraParagraphSpacing = 7;
  p.embeddedStyle = 7;
  p.focusReadingEnabled = 7;
  const ReaderRenderSpec spec = makeRenderSpec(p, 1, 480, 800);
  EXPECT_TRUE(spec.extraParagraphSpacing);
  EXPECT_TRUE(spec.embeddedStyle);
  EXPECT_TRUE(spec.focusReadingEnabled);
}

TEST(RenderSpec, LineSpacingRangeIs80To175) {
  EXPECT_EQ(80, reader_defaults::MIN_LINE_SPACING_PERCENT);
  EXPECT_EQ(175, reader_defaults::MAX_LINE_SPACING_PERCENT);
  ReaderPrefs p;
  p.lineSpacingPercent = 95;
  EXPECT_FLOAT_EQ(0.95f, makeRenderSpec(p, 1, 480, 800).lineCompression);
}

TEST(RenderSpec, BlackAndWhiteImagesLayOutAsDisplayAndOthersKeepTheirMode) {
  ReaderPrefs p;
  for (uint8_t mode : {0, 1, 2}) {
    p.imageRendering = mode;
    EXPECT_EQ(mode, makeRenderSpec(p, 1, 480, 800).imageRendering);
  }
  p.imageRendering = reader_defaults::IMAGES_DISPLAY_BW;
  EXPECT_EQ(0, makeRenderSpec(p, 1, 480, 800).imageRendering);
}

TEST(RenderSpec, IndentAndCharacterSpacingReachTheSpecInTheirUnits) {
  ReaderPrefs p;
  p.paragraphIndentSpaces = 9;  // out of range: clamped to five spaces
  p.characterSpacing = reader_defaults::CHARACTER_SPACING + 2;
  const ReaderRenderSpec spec = makeRenderSpec(p, 1, 480, 800);
  EXPECT_EQ(5, spec.paragraphIndentSpaces);
  EXPECT_EQ(2, spec.characterSpacing);
  p.characterSpacing = 0;
  EXPECT_EQ(-2, makeRenderSpec(p, 1, 480, 800).characterSpacing);
}

TEST(RenderSpec, CallerSuppliesFontAndViewport) {
  const ReaderRenderSpec spec = makeRenderSpec(ReaderPrefs{}, 42, 480, 800);
  EXPECT_EQ(spec.fontId, 42);
  EXPECT_EQ(spec.viewportWidth, 480);
  EXPECT_EQ(spec.viewportHeight, 800);
}

// Every Reader Settings row either changes the spec or is named in NOT_IN_SPEC. A new
// look field that the builder forgets fails here, and since the preview keys on the same
// spec, so would one the preview forgets.
TEST(RenderSpec, EveryLookFieldReachesTheSpecOrIsListedAsNot) {
  const ReaderRenderSpec base = makeRenderSpec(baseLook(), 1, 480, 800);
#define CP_CHECK_FIELD(name)                                                             \
  {                                                                                      \
    ReaderPrefs p = baseLook();                                                          \
    p.name ^= 1;                                                                         \
    const bool changed = !(makeRenderSpec(p, 1, 480, 800) == base);                      \
    EXPECT_NE(changed, NOT_IN_SPEC.count(#name) == 1)                                    \
        << #name << (changed ? " is" : " is not") << " in the spec; update NOT_IN_SPEC"; \
  }
  READER_LOOK_SCREEN_FIELDS(CP_CHECK_FIELD)
#undef CP_CHECK_FIELD
}

TEST(RenderSpec, SectionCacheMatchIsMemberwise) {
  const ReaderRenderSpec a = makeRenderSpec(ReaderPrefs{}, 1, 480, 800);
  ReaderRenderSpec b = a;
  EXPECT_TRUE(sectionCacheMatches(a, b));
  b.paragraphIndentSpaces++;
  EXPECT_FALSE(sectionCacheMatches(a, b));
}
