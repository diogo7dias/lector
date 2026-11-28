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

// Images: Display (B/W): one 2x2 cell inks 4 of 4 for black, 3 for dark gray, 1 for light
// gray and none for white, whatever the cell's position.
TEST(BwInkFromGray4, CellCoverageMatchesTheGrayLevel) {
  constexpr int INKED_OF_FOUR[4] = {4, 3, 1, 0};
  for (int level = 0; level < 4; ++level) {
    for (int cellY = 0; cellY < 6; cellY += 2) {
      for (int cellX = 0; cellX < 6; cellX += 2) {
        int inked = 0;
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) inked += bwInkFromGray4(static_cast<uint8_t>(level), cellX + dx, cellY + dy);
        EXPECT_EQ(INKED_OF_FOUR[level], inked) << level << " at " << cellX << "," << cellY;
      }
    }
  }
}

// Night mode skips the hardware grayscale pass, so BW pass must force Bayer dithering
// whenever the display is inverted — otherwise gray levels 1 and 2 crush to solid blobs.
TEST(NightModeImageDither, ForcesBayerDitherWhenDisplayInverted) {
  EXPECT_FALSE(shouldDitherImagesInBw(/*imagesDisplayBw=*/false, /*displayInverted=*/false));
  EXPECT_TRUE(shouldDitherImagesInBw(/*imagesDisplayBw=*/true, /*displayInverted=*/false));
  EXPECT_TRUE(shouldDitherImagesInBw(/*imagesDisplayBw=*/false, /*displayInverted=*/true));
  EXPECT_TRUE(shouldDitherImagesInBw(/*imagesDisplayBw=*/true, /*displayInverted=*/true));
}

// In night mode, Normal Polarity pre-inverts the image rectangle so the display's hardware
// inversion cancels out and photos keep natural 4-level dithered tones (4/3/1/0 ink per 2x2).
// Invert leaves the image un-flipped so diagrams render white-on-black (0/1/3/4 ink per 2x2).
TEST(NightModeImageDither, NormalPolarityVsInvertProducesComplementaryTonesOnPanel) {
  constexpr int NATURAL_INK[4] = {4, 3, 1, 0};
  constexpr int INVERTED_INK[4] = {0, 1, 3, 4};
  for (int level = 0; level < 4; ++level) {
    int normalPanelInk = 0;
    int invertPanelInk = 0;
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        const bool fbInk = bwInkFromGray4(static_cast<uint8_t>(level), x, y);
        // Normal Polarity: preserveImagePolarity flips fbInk, then display inversion flips again.
        const bool normalFbAfterPreserve = shouldPreserveImagePolarity(/*displayInverted=*/true,
                                                                       /*invertImagesInNightMode=*/false)
                                               ? !fbInk
                                               : fbInk;
        const bool invertFbAfterPreserve = shouldPreserveImagePolarity(/*displayInverted=*/true,
                                                                       /*invertImagesInNightMode=*/true)
                                               ? !fbInk
                                               : fbInk;
        // Hardware display inversion flips FB ink on the physical panel.
        normalPanelInk += (!normalFbAfterPreserve) ? 1 : 0;
        invertPanelInk += (!invertFbAfterPreserve) ? 1 : 0;
      }
    }
    EXPECT_EQ(NATURAL_INK[level], normalPanelInk) << "Normal polarity level " << level;
    EXPECT_EQ(INVERTED_INK[level], invertPanelInk) << "Invert polarity level " << level;
  }
}

}  // namespace
