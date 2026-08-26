#include <gtest/gtest.h>

#include "util/BoundActionScope.h"

using namespace bound_action;

TEST(BoundActionScope, NavigationActionsWorkAnywhere) {
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_PAGE_PREV));
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_PAGE_NEXT));
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_GO_HOME));
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_BACK));
}

TEST(BoundActionScope, DeviceActionsWorkAnywhere) {
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_LIGHT_PANEL));
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_SLEEP));
}

TEST(BoundActionScope, DisabledIsAllowedSoAnEmptyBindingIsNeverGreyed) {
  EXPECT_TRUE(allowedOutsideBook(LP_MENU_DISABLED));
}

TEST(BoundActionScope, ActionsThatNeedAnOpenBookAreRefused) {
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_BOOKMARK));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_DICTIONARY));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_GRAB_QUOTE));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_SELECT_CHAPTER));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_GO_TO_PERCENT));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_GO_TO_PARAGRAPH));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_FOOTNOTES));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_TEXT_SETTINGS));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_READER_SETTINGS));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_TOGGLE_STATUS_BAR));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_BOOKMARKS));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_VIEW_QUOTES));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_KOSYNC));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_NEARBY_SEND_BOOK));
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_WALLPAPER_HOLD));
}

TEST(BoundActionScope, TheReaderQuickMenuIsRefusedOutsideABook) {
  // The pop-up lists in-book actions, so offering it outside would open a menu
  // whose every row is unavailable.
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_POPUP));
}

TEST(BoundActionScope, ForceRefreshWorksAnywhere) { EXPECT_TRUE(allowedOutsideBook(LP_MENU_FORCE_REFRESH)); }

TEST(BoundActionScope, DeletingAWallpaperNeedsAnOpenBook) {
  // It runs through the in-book menu's own confirmation, so it is offered only where that
  // menu is: the reader.
  EXPECT_FALSE(allowedOutsideBook(LP_MENU_WALLPAPER_DELETE));
}
