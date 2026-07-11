#include <gtest/gtest.h>

#include <vector>

#include "activities/home/ImageViewerPatch.h"

TEST(ImageViewerPatch, SiblingNavigationWithoutMutationDoesNotPatchLaunchRow) {
  const auto patch = image_viewer_patch::plan("/sleep/B.bmp", "");

  EXPECT_TRUE(patch.valid);
  EXPECT_EQ(patch.action, image_viewer_patch::Action::None);
}

TEST(ImageViewerPatch, MovedSiblingErasesSiblingRow) {
  const auto patch = image_viewer_patch::plan("", "/sleep/B.bmp");

  EXPECT_TRUE(patch.valid);
  EXPECT_EQ(patch.action, image_viewer_patch::Action::Erase);
  EXPECT_EQ(patch.sourceName, "B.bmp");
}

TEST(ImageViewerPatch, FavoriteRenamePatchesOriginalRow) {
  const auto patch = image_viewer_patch::plan("/sleep/A_F.pxc", "/sleep/A.pxc");

  EXPECT_TRUE(patch.valid);
  EXPECT_EQ(patch.action, image_viewer_patch::Action::Rename);
  EXPECT_EQ(patch.sourceName, "A.pxc");
  EXPECT_EQ(patch.finalName, "A_F.pxc");
}

TEST(ImageViewerPatch, MovedSiblingSelectsItsOldBrowserSlot) {
  EXPECT_EQ(image_viewer_patch::selectorForSource(3, 2, nullptr), 5U);

  const std::vector<size_t> filteredIndexes{1, 3, 7};
  EXPECT_EQ(image_viewer_patch::selectorForSource(3, 2, &filteredIndexes), 3U);
  EXPECT_FALSE(image_viewer_patch::selectorForSource(5, 2, &filteredIndexes).has_value());
}
