#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "ReaderRenderSpec.h"

TEST(SectionCacheValidity, IdenticalDecodedParametersMatch) {
  ReaderRenderSpec requested;
  requested.fontId = 14;
  requested.viewportWidth = 440;
  requested.viewportHeight = 720;
  requested.guideDotsMode = GUIDE_DOTS_HIDDEN;
  EXPECT_TRUE(sectionCacheMatches(requested, requested));
}

TEST(SectionCacheValidity, EveryLayoutFieldInvalidatesIndependently) {
  const ReaderRenderSpec requested;
#define CHECK_FIELD(field, value)                                   \
  {                                                                 \
    ReaderRenderSpec cached = requested;                            \
    cached.field = value;                                           \
    EXPECT_FALSE(sectionCacheMatches(requested, cached)) << #field; \
  }
  CHECK_FIELD(fontId, 1);
  CHECK_FIELD(lineCompression, 1.1f);
  CHECK_FIELD(extraParagraphSpacing, true);
  CHECK_FIELD(paragraphSpacing, 10);
  CHECK_FIELD(wordSpacing, 125);
  CHECK_FIELD(paragraphAlignment, 1);
  CHECK_FIELD(viewportWidth, 440);
  CHECK_FIELD(viewportHeight, 720);
  CHECK_FIELD(embeddedTextStyle, false);
  CHECK_FIELD(embeddedLayoutStyle, false);
  CHECK_FIELD(imageRendering, 1);
  CHECK_FIELD(focusReadingEnabled, true);
  CHECK_FIELD(guideDotsMode, GUIDE_DOTS_VISIBLE);
  CHECK_FIELD(guideDotsMode, GUIDE_DOTS_HIDDEN);
  CHECK_FIELD(firstLineIndentMode, 1);
  CHECK_FIELD(firstLineIndentPercent, 20);
#undef CHECK_FIELD
}

TEST(SectionCacheValidity, FloatComparisonRemainsExactAndRejectsNaN) {
  ReaderRenderSpec requested;
  ReaderRenderSpec cached = requested;
  cached.lineCompression = std::nextafter(requested.lineCompression, 2.0f);
  EXPECT_FALSE(sectionCacheMatches(requested, cached));
  cached.lineCompression = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(sectionCacheMatches(requested, cached));
  EXPECT_FALSE(sectionCacheMatches(cached, cached));
  requested.lineCompression = 0.0f;
  cached.lineCompression = -0.0f;
  EXPECT_TRUE(sectionCacheMatches(requested, cached));
}
