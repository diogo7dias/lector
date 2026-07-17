// Host tests for ImageToFramebufferDecoder::validateImageDimensions.
//
// Regression focus: the original check computed `width * height` in 32-bit
// int, so a hostile/corrupt header like 60000x60000 (3.6e9) wrapped negative
// and slipped past the MAX_SOURCE_PIXELS cap. The fix rejects non-positive
// dimensions, caps each axis, and does the pixel product in 64-bit.

#include <gtest/gtest.h>

#include "ImageToFramebufferDecoder.h"

namespace {

class TestDecoder : public ImageToFramebufferDecoder {
 public:
  bool decodeToFramebuffer(const std::string&, GfxRenderer&, const RenderConfig&) override { return false; }
  bool getDimensions(const std::string&, ImageDimensions&) const override { return false; }
  const char* getFormatName() const override { return "TEST"; }

  bool validate(int width, int height) { return validateImageDimensions(width, height, "TEST"); }
};

TEST(ImageDimensionPolicy, AcceptsTypicalCoverSizes) {
  TestDecoder d;
  EXPECT_TRUE(d.validate(600, 800));
  EXPECT_TRUE(d.validate(1600, 1900));
  EXPECT_TRUE(d.validate(2048, 1536));  // exactly MAX_SOURCE_PIXELS
  EXPECT_TRUE(d.validate(1, 1));
}

TEST(ImageDimensionPolicy, RejectsOverPixelBudget) {
  TestDecoder d;
  EXPECT_FALSE(d.validate(2048, 1537));
  EXPECT_FALSE(d.validate(4000, 4000));
}

TEST(ImageDimensionPolicy, RejectsInt32OverflowDimensions) {
  TestDecoder d;
  // 60000 * 60000 = 3.6e9 wraps negative in int32; must still be rejected.
  EXPECT_FALSE(d.validate(60000, 60000));
  // 65536 * 65536 = 2^32 wraps to exactly 0 in uint32 / int32.
  EXPECT_FALSE(d.validate(65536, 65536));
  EXPECT_FALSE(d.validate(2000000000, 2));
}

TEST(ImageDimensionPolicy, RejectsAbsurdSingleAxis) {
  TestDecoder d;
  // Tall-and-thin: pixel product is tiny but the axis alone is absurd.
  EXPECT_FALSE(d.validate(1, 1000000));
  EXPECT_FALSE(d.validate(1000000, 1));
}

TEST(ImageDimensionPolicy, RejectsNonPositiveDimensions) {
  TestDecoder d;
  EXPECT_FALSE(d.validate(0, 100));
  EXPECT_FALSE(d.validate(100, 0));
  EXPECT_FALSE(d.validate(-1, 100));
  EXPECT_FALSE(d.validate(100, -100));
}

}  // namespace
