#include <gtest/gtest.h>

#include "sleep/WallpaperDirectPickPolicy.h"

TEST(WallpaperDirectPickPolicy, UsesSavedOrderOnlyWhenCandidateStillExists) {
  EXPECT_EQ(wallpaper_direct_pick::source(/*hasSavedCandidate=*/true, /*savedCandidateExists=*/true),
            wallpaper_direct_pick::Source::SavedOrder);
  EXPECT_EQ(wallpaper_direct_pick::source(/*hasSavedCandidate=*/true, /*savedCandidateExists=*/false),
            wallpaper_direct_pick::Source::LiveFolder);
  EXPECT_EQ(wallpaper_direct_pick::source(/*hasSavedCandidate=*/false, /*savedCandidateExists=*/false),
            wallpaper_direct_pick::Source::LiveFolder);
}

TEST(WallpaperDirectPickPolicy, SuccessfulSleepFolderMoveInvalidatesSavedOrder) {
  EXPECT_TRUE(wallpaper_direct_pick::shouldMarkFolderDirty(/*moveSucceeded=*/true));
  EXPECT_FALSE(wallpaper_direct_pick::shouldMarkFolderDirty(/*moveSucceeded=*/false));
}
