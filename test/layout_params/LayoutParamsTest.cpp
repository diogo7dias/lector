// Host tests for LayoutParams — the single value type that identifies a laid-out
// section cache. This is the regression the "font/size change did nothing" bug
// needed: every layout-affecting field MUST change hash(); nothing else may.
//
// hash() is pure (no HalFile / SETTINGS / renderer), so it is fully host-testable.

#include <gtest/gtest.h>

#include "LayoutParams.h"

namespace {

// A non-default baseline so a field flip is never masked by already equalling
// the default.
LayoutParams baseline() {
  LayoutParams p;
  p.fontId = 42;
  p.lineCompression = 1.2f;
  p.extraParagraphSpacing = true;
  p.paragraphAlignment = 2;
  p.viewportWidth = 600;
  p.viewportHeight = 400;
  p.hyphenationEnabled = true;
  p.embeddedStyle = true;
  p.imageRendering = 1;
  p.focusReadingEnabled = true;
  p.guideDotsEnabled = true;
  p.firstLineIndentPx = 24;
  p.wordSpacing = 5;
  p.paragraphSpacing = 30;
  return p;
}

TEST(LayoutParams, EqualParamsHashEqual) { EXPECT_EQ(baseline().hash(), baseline().hash()); }

TEST(LayoutParams, DefaultDiffersFromBaseline) { EXPECT_NE(LayoutParams{}.hash(), baseline().hash()); }

// Each layout-affecting field, flipped from the baseline, must move the hash.
// If any of these fails, a change to that setting would render a stale cached
// layout as a cache HIT — exactly the font bug.

TEST(LayoutParams, FontIdChangesHash) {
  LayoutParams p = baseline();
  p.fontId = 43;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, LineCompressionChangesHash) {
  LayoutParams p = baseline();
  p.lineCompression = 1.3f;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ExtraParagraphSpacingChangesHash) {
  LayoutParams p = baseline();
  p.extraParagraphSpacing = false;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ParagraphAlignmentChangesHash) {
  LayoutParams p = baseline();
  p.paragraphAlignment = 3;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ViewportWidthChangesHash) {
  LayoutParams p = baseline();
  p.viewportWidth = 601;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ViewportHeightChangesHash) {
  LayoutParams p = baseline();
  p.viewportHeight = 401;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, HyphenationChangesHash) {
  LayoutParams p = baseline();
  p.hyphenationEnabled = false;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, EmbeddedStyleChangesHash) {
  LayoutParams p = baseline();
  p.embeddedStyle = false;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ImageRenderingChangesHash) {
  LayoutParams p = baseline();
  p.imageRendering = 2;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, FocusReadingChangesHash) {
  LayoutParams p = baseline();
  p.focusReadingEnabled = false;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, GuideDotsChangesHash) {
  LayoutParams p = baseline();
  p.guideDotsEnabled = false;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, FirstLineIndentChangesHash) {
  LayoutParams p = baseline();
  p.firstLineIndentPx = 25;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, WordSpacingChangesHash) {
  LayoutParams p = baseline();
  p.wordSpacing = 6;
  EXPECT_NE(p.hash(), baseline().hash());
}

TEST(LayoutParams, ParagraphSpacingChangesHash) {
  LayoutParams p = baseline();
  p.paragraphSpacing = 31;
  EXPECT_NE(p.hash(), baseline().hash());
}

// The defaulted operator== must compare every field — used by the cache-load
// path to accept/reject a section file.
TEST(LayoutParams, EqualityIsFieldwise) {
  EXPECT_TRUE(baseline() == baseline());
  LayoutParams p = baseline();
  p.wordSpacing = 99;
  EXPECT_FALSE(p == baseline());
}

}  // namespace
