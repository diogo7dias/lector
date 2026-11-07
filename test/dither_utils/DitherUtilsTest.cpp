#include <gtest/gtest.h>

#include <cstdlib>

#include "lib/Epub/Epub/converters/DitherUtils.h"

namespace {

// Inline images take the curve covers use, so a picture looks the same in both places.
TEST(Quantize4Level, PlainMatchesCoverCurve) {
  for (int gray = 0; gray <= 255; ++gray) {
    EXPECT_EQ(quantizeGray4(gray, Gray4QuantizationMode::DisplayTuned).index,
              quantize4Level(static_cast<uint8_t>(gray), 0, 0, false))
        << gray;
  }
}

// Over one 4x4 tile the shown levels average back to the input gray, never darker for a
// lighter input, and the extremes stay solid.
TEST(Quantize4Level, DitherAveragesToTheInputOnPanelLevels) {
  int previousSum = -1;
  for (int gray = 0; gray <= 255; ++gray) {
    int sum = 0;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) sum += DISPLAY_TUNED_GRAY4[quantize4Level(static_cast<uint8_t>(gray), x, y, true)];
    EXPECT_GE(sum, previousSum) << gray;
    previousSum = sum;
    const int clamped = std::max<int>(DISPLAY_TUNED_GRAY4[0], std::min<int>(gray, DISPLAY_TUNED_GRAY4[3]));
    // One of 16 cells off at most: the span between the two levels around gray, over 16.
    EXPECT_LE(std::abs(sum - clamped * 16), DISPLAY_TUNED_GRAY4[3] - DISPLAY_TUNED_GRAY4[2]) << gray;
  }
  EXPECT_EQ(0, quantize4Level(0, 3, 1, true));
  EXPECT_EQ(3, quantize4Level(255, 3, 1, true));
}

}  // namespace
