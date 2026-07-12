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

TEST(WallpaperDirectPickPolicy, RepeatSkippedWhenRotatingWithMultipleFiles) {
  // Rotation active, the chosen file equals the last one shown, and there is more
  // than one wallpaper: reject it so the next sleep differs.
  EXPECT_TRUE(wallpaper_direct_pick::isImmediateRepeat(/*rotationPaused=*/false, /*sameAsLastShown=*/true,
                                                       /*moreThanOneFile=*/true));
}

TEST(WallpaperDirectPickPolicy, DifferentCandidateIsNeverARepeat) {
  EXPECT_FALSE(wallpaper_direct_pick::isImmediateRepeat(/*rotationPaused=*/false, /*sameAsLastShown=*/false,
                                                        /*moreThanOneFile=*/true));
}

TEST(WallpaperDirectPickPolicy, PausedRotationMayRepeat) {
  // When rotation is paused, re-showing the same wallpaper is intended.
  EXPECT_FALSE(wallpaper_direct_pick::isImmediateRepeat(/*rotationPaused=*/true, /*sameAsLastShown=*/true,
                                                        /*moreThanOneFile=*/true));
}

TEST(WallpaperDirectPickPolicy, SingleFileMayRepeat) {
  // With only one wallpaper there is nothing else to show; skipping would leave
  // no image, so it is not a skippable repeat.
  EXPECT_FALSE(wallpaper_direct_pick::isImmediateRepeat(/*rotationPaused=*/false, /*sameAsLastShown=*/true,
                                                        /*moreThanOneFile=*/false));
}
