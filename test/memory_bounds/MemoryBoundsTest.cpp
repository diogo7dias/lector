#include <Memory/GrowthBounds.h>
#include <gtest/gtest.h>

#include <limits>

TEST(GrowthBounds, AcceptsAppendWithinLimit) { EXPECT_TRUE(memory::canGrowWithinLimit(100, 20, 120)); }

TEST(GrowthBounds, RejectsAppendBeyondLimit) { EXPECT_FALSE(memory::canGrowWithinLimit(100, 21, 120)); }

TEST(GrowthBounds, RejectsOverflowWithoutOverflowing) {
  EXPECT_FALSE(
      memory::canGrowWithinLimit(std::numeric_limits<size_t>::max() - 2, 4, std::numeric_limits<size_t>::max()));
}
