#include <gtest/gtest.h>

#include "activities/util/ImageViewerPolicy.h"

TEST(ImageViewerPolicy, BrowserSleepViewerDefersSiblingLoad) {
  EXPECT_FALSE(image_viewer_policy::loadSiblingsOnEnter(/*resultMode=*/true, /*inSleepDirectory=*/true));
}

TEST(ImageViewerPolicy, BrowserViewerOutsideSleepKeepsEagerLoad) {
  EXPECT_TRUE(image_viewer_policy::loadSiblingsOnEnter(/*resultMode=*/true, /*inSleepDirectory=*/false));
}

TEST(ImageViewerPolicy, StandaloneSleepViewerKeepsEagerLoad) {
  EXPECT_TRUE(image_viewer_policy::loadSiblingsOnEnter(/*resultMode=*/false, /*inSleepDirectory=*/true));
}

TEST(ImageViewerPolicy, UnloadedLazyViewerShowsNavigationHints) {
  EXPECT_TRUE(image_viewer_policy::showUnloadedNavigationHints(/*resultMode=*/true, /*inSleepDirectory=*/true,
                                                               /*siblingsLoaded=*/false));
  EXPECT_FALSE(image_viewer_policy::showUnloadedNavigationHints(/*resultMode=*/true, /*inSleepDirectory=*/true,
                                                                /*siblingsLoaded=*/true));
}
