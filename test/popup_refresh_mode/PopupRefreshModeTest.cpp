#include <gtest/gtest.h>

#include "src/components/PopupRefreshMode.h"

TEST(PopupRefreshModeTest, DefaultBannerUsesCleanRefresh) {
  EXPECT_EQ(popupRefreshMode(), PopupRefresh::Clean);
}

TEST(PopupRefreshModeTest, TemporaryBannerUsesFastRefresh) {
  EXPECT_EQ(popupRefreshMode(PopupRefresh::Temporary), PopupRefresh::Temporary);
}
