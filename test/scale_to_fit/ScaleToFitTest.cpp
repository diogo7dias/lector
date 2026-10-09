#include <gtest/gtest.h>

#include "lib/GfxRenderer/BitmapHelpers.h"

namespace {

struct Size {
  int width;
  int height;
};

Size fit(const int srcW, const int srcH, const int targetW, const int targetH, const bool crop) {
  Size out{};
  scaleToFit(srcW, srcH, targetW, targetH, crop, out.width, out.height);
  return out;
}

}  // namespace

TEST(ScaleToFit, FitKeepsAspectInsideTarget) {
  const Size out = fit(1600, 2560, 480, 800, false);
  EXPECT_EQ(out.width, 480);
  EXPECT_EQ(out.height, 768);
}

TEST(ScaleToFit, CropCoversTarget) {
  const Size out = fit(1600, 2560, 480, 800, true);
  EXPECT_EQ(out.width, 500);
  EXPECT_EQ(out.height, 800);
}

TEST(ScaleToFit, CropOfASliverFallsBackToFit) {
  int w = 0, h = 0;
  scaleToFit(2, 3072, 480, 800, /*crop=*/true, w, h);
  EXPECT_LE(h, 800);
  EXPECT_LE(w, 480);
}

TEST(ScaleToFit, UpscalesSmallSource) {
  const Size out = fit(100, 50, 400, 400, false);
  EXPECT_EQ(out.width, 400);
  EXPECT_EQ(out.height, 200);
}

TEST(ScaleToFit, TruncatesTowardZero) {
  // Scale 1000/7 leaves 3 * 142.857 = 428.57 wide: truncated, never rounded up.
  const Size out = fit(3, 7, 800, 1000, false);
  EXPECT_EQ(out.width, 428);
}

TEST(ScaleToFit, ClampsEachSideToOnePixel) {
  const Size out = fit(3000, 2, 10, 10, false);
  EXPECT_EQ(out.width, 10);
  EXPECT_EQ(out.height, 1);
}
