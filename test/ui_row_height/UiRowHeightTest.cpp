#include <gtest/gtest.h>

#include "components/UiRowHeight.h"

namespace {

// The SDK derives its row height from the body line height (lineHeight * 2 + 8),
// sized for a label plus a subtitle. Lector's lists are mostly single-line, so
// that row reads as a third of a screen wasted.
constexpr int kLineHeight = 25;
constexpr int kSdkRow = kLineHeight * 2 + 8;  // 58

TEST(UiRowHeight, KeepsSeventyPercentOfTheDerivedRow) { EXPECT_EQ(ui_row_height::scaled(kSdkRow, kLineHeight), 40); }

TEST(UiRowHeight, NeverShorterThanTheTextItHolds) {
  // A large reading font with a row that was already tight: shrinking it would
  // clip the label rather than save space.
  EXPECT_EQ(ui_row_height::scaled(40, 38), 38 + ui_row_height::kMinPadding);
}

TEST(UiRowHeight, AnUnsetRowStaysUnset) {
  // 0 is the inherit-from-theme sentinel in ListProps; scaling it would turn
  // "not set" into a real height.
  EXPECT_EQ(ui_row_height::scaled(0, kLineHeight), 0);
  EXPECT_EQ(ui_row_height::scaled(-1, kLineHeight), -1);
}

TEST(UiRowHeight, AnUnknownLineHeightOnlyScales) {
  // Before the first font is bound the target reports no line height; without
  // a floor to apply, the ratio is still safe.
  EXPECT_EQ(ui_row_height::scaled(kSdkRow, 0), 40);
}

}  // namespace
