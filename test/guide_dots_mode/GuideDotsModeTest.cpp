// The Style tab carries Guide Dots and Hidden Dots as two independent toggles, but the
// layout engine and the section-cache header key on a single mode byte. These pin the
// collapse, including the state the UI hides but the settings file can still hold:
// Hidden Dots left on after Guide Dots was switched off.
#include <gtest/gtest.h>

#include "Epub/ReaderRenderSpec.h"

TEST(GuideDotsMode, DotsOffIsOffWhateverHiddenSays) {
  EXPECT_EQ(resolveGuideDotsMode(0, 0), GUIDE_DOTS_OFF);
  EXPECT_EQ(resolveGuideDotsMode(0, 1), GUIDE_DOTS_OFF);
}

TEST(GuideDotsMode, DotsOnDrawsThemUntilHiddenIsSet) {
  EXPECT_EQ(resolveGuideDotsMode(1, 0), GUIDE_DOTS_VISIBLE);
  EXPECT_EQ(resolveGuideDotsMode(1, 1), GUIDE_DOTS_HIDDEN);
}

// Hidden Dots must be its own cache-key state: it changes what the cached blocks carry
// (no per-word dot offsets), so a book cached with dots visible has to be rebuilt.
TEST(GuideDotsMode, HiddenIsDistinctFromBothOtherStates) {
  EXPECT_NE(resolveGuideDotsMode(1, 1), resolveGuideDotsMode(1, 0));
  EXPECT_NE(resolveGuideDotsMode(1, 1), resolveGuideDotsMode(0, 0));
}

// The mode reuses the byte the section header has carried since v37, so the spec field
// must stay one byte or every cached book silently mismatches.
TEST(GuideDotsMode, ModeStaysOneByteInTheRenderSpec) {
  ReaderRenderSpec spec;
  EXPECT_EQ(sizeof(spec.guideDotsMode), 1u);
  EXPECT_EQ(spec.guideDotsMode, GUIDE_DOTS_OFF);
}
