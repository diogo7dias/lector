#include <gtest/gtest.h>

#include "activities/boot_sleep/StatsDashboardPolicy.h"

using namespace stats_dashboard;

TEST(StatsDashboardPolicy, RecognizesOnlyDashboardModes) {
  EXPECT_FALSE(isDashboardMode(8));
  EXPECT_TRUE(isDashboardMode(kStatsDashboardMode));
  EXPECT_TRUE(isDashboardMode(kStatsDashboardPlusMode));
  EXPECT_FALSE(isDashboardMode(11));
}

TEST(StatsDashboardPolicy, PlusUsesWallpaperAndStandardUsesCover) {
  EXPECT_FALSE(usesWallpaper(kStatsDashboardMode));
  EXPECT_TRUE(usesWallpaper(kStatsDashboardPlusMode));
}

TEST(StatsDashboardPolicy, SupportsEveryBookTypeWithReadingStats) {
  EXPECT_TRUE(supportsBook("/Books/novel.epub"));
  EXPECT_TRUE(supportsBook("/Books/comic.XTCH"));
  EXPECT_TRUE(supportsBook("/Books/notes.txt"));
  EXPECT_TRUE(supportsBook("/Books/essay.MD"));
  EXPECT_FALSE(supportsBook("/Books/manual.pdf"));
}
