// Host tests for makeRenderSpec(), the one builder behind the reader's section cache key
// and the Text Settings preview key.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "ReaderPrefs.h"

namespace {

// Reader Settings rows that do not reach the spec: the font is resolved by the caller,
// the margins become the viewport, and the rest are draw-only or hard-set.
const std::set<std::string> NOT_IN_SPEC = {
    "fontFamily",     "fontPointSize",  "screenMargin",     "screenMarginTop", "screenMarginBottom",
    "marginLinkMode", "dynamicMargins", "textAntiAliasing", "imageRendering",
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

TEST(RenderSpec, NormalisesSwitchesAndHardSetsImages) {
  ReaderPrefs p;
  p.extraParagraphSpacing = 7;
  p.embeddedTextStyle = 7;
  p.embeddedLayoutStyle = 7;
  p.focusReadingEnabled = 7;
  p.imageRendering = 2;
  const ReaderRenderSpec spec = makeRenderSpec(p, 1, 480, 800);
  EXPECT_TRUE(spec.extraParagraphSpacing);
  EXPECT_TRUE(spec.embeddedTextStyle);
  EXPECT_TRUE(spec.embeddedLayoutStyle);
  EXPECT_TRUE(spec.focusReadingEnabled);
  EXPECT_EQ(spec.imageRendering, 0);
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
  b.firstLineIndentPercent++;
  EXPECT_FALSE(sectionCacheMatches(a, b));
}
