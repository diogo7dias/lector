#include <gtest/gtest.h>

#include "ReaderFontSizes.h"

// The built-in ChareInk sizes: the list the pickers offer and the scaled-text variant rule.
TEST(BuiltinSizes, ListIsTwelveFourteenSixteen) {
  ASSERT_EQ(3u, std::size(BUILTIN_READER_POINT_SIZES));
  EXPECT_EQ(12, BUILTIN_READER_POINT_SIZES[0]);
  EXPECT_EQ(14, BUILTIN_READER_POINT_SIZES[1]);
  EXPECT_EQ(16, BUILTIN_READER_POINT_SIZES[2]);
}

TEST(BuiltinSizes, IdsAreDistinct) {
  EXPECT_NE(CHAREINK_12_FONT_ID, CHAREINK_14_FONT_ID);
  EXPECT_NE(CHAREINK_14_FONT_ID, CHAREINK_16_FONT_ID);
  EXPECT_NE(CHAREINK_12_FONT_ID, CHAREINK_16_FONT_ID);
}

TEST(BuiltinSizes, ScaledTextPicksNearestSize) {
  EXPECT_EQ(CHAREINK_12_FONT_ID,
            builtinReaderFontVariant(CHAREINK_14_FONT_ID, 0.8f));  // 11.2: shrink rounds down, floor 12
  EXPECT_EQ(CHAREINK_16_FONT_ID, builtinReaderFontVariant(CHAREINK_14_FONT_ID, 1.2f));    // 16.8
  EXPECT_EQ(CHAREINK_16_FONT_ID, builtinReaderFontVariant(CHAREINK_14_FONT_ID, 2.0f));    // 28: largest
  EXPECT_EQ(CHAREINK_14_FONT_ID, builtinReaderFontVariant(CHAREINK_16_FONT_ID, 0.875f));  // 14
  EXPECT_EQ(CHAREINK_12_FONT_ID, builtinReaderFontVariant(CHAREINK_16_FONT_ID, 0.7f));    // 11.2: floor 12
  EXPECT_EQ(CHAREINK_12_FONT_ID, builtinReaderFontVariant(CHAREINK_12_FONT_ID, 1.0f));
  EXPECT_EQ(0, builtinReaderFontVariant(123, 1.5f));  // not a ChareInk id
}
