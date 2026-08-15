// Host proof of which folder the wallpaper rotation reads.
//
// The rule exists because an empty /.sleep used to win over a populated /sleep,
// which made every wallpaper on the card invisible with no error anywhere: the
// index indexed an empty folder, the boot gate's tail probe never moved, and
// the sleep screen fell back to the default art.

#include <gtest/gtest.h>

#include "sleep/SleepFolderPolicy.h"

using sleep_folder::chooseDirId;
using sleep_folder::kHiddenDirId;
using sleep_folder::kPlainDirId;

// The visible folder is what a computer shows when the card is mounted, so it
// wins whenever it actually holds wallpapers.
TEST(SleepFolderPolicy, PlainFolderWinsWhenItHasWallpapers) {
  EXPECT_EQ(chooseDirId(/*plainHasEntries=*/true, /*hiddenHasEntries=*/false), kPlainDirId);
  EXPECT_EQ(chooseDirId(/*plainHasEntries=*/true, /*hiddenHasEntries=*/true), kPlainDirId);
}

// Cards laid out by older firmware keep working: /.sleep is still read when it
// is the only folder holding anything.
TEST(SleepFolderPolicy, HiddenFolderIsTheFallback) {
  EXPECT_EQ(chooseDirId(/*plainHasEntries=*/false, /*hiddenHasEntries=*/true), kHiddenDirId);
}

// The exact bug this rule was written for: an empty /.sleep must not shadow a
// populated /sleep.
TEST(SleepFolderPolicy, EmptyHiddenFolderNeverShadowsThePlainOne) {
  EXPECT_EQ(chooseDirId(/*plainHasEntries=*/true, /*hiddenHasEntries=*/false), kPlainDirId);
}

// Nothing anywhere resolves to the plain folder, so a first fill (and the empty
// index built for it) is anchored on the folder a user can see.
TEST(SleepFolderPolicy, NeitherFolderResolvesToThePlainOne) {
  EXPECT_EQ(chooseDirId(/*plainHasEntries=*/false, /*hiddenHasEntries=*/false), kPlainDirId);
}
