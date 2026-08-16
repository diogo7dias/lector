// Host proof of two folder-level rules the wallpaper rotation depends on:
// which folder it reads, and whether that folder looks changed since the last
// reconcile.
//
// The first rule exists because an empty /.sleep used to win over a populated
// /sleep, which made every wallpaper on the card invisible with no error
// anywhere: the index indexed an empty folder, the boot gate's tail probe never
// moved, and the sleep screen fell back to the default art.

#include <gtest/gtest.h>

#include "sleep/SleepFolderPolicy.h"

using sleep_folder::chooseDirId;
using sleep_folder::folderMarkersChanged;
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

// An untouched folder must not cost a walk — that is the whole point of the
// boot gate.
TEST(SleepFolderMarkers, IdenticalMarkersAreNoChange) {
  EXPECT_FALSE(folderMarkersChanged(/*tailNow=*/1200, /*stampNow=*/0x5A3C1111, /*tailSaved=*/1200,
                                    /*stampSaved=*/0x5A3C1111));
}

// The slot tail moving is the classic signal: files appended to a FAT directory
// push the last live entry further out.
TEST(SleepFolderMarkers, TailMovementIsAChange) {
  EXPECT_TRUE(folderMarkersChanged(/*tailNow=*/1500, /*stampNow=*/0x5A3C1111, /*tailSaved=*/1200,
                                   /*stampSaved=*/0x5A3C1111));
}

// The reason the timestamp exists. A FAT delete frees its slots in place and
// the directory never shrinks, so files written later can land in those holes
// and leave the tail byte-identical. The folder's own modify time still moves,
// so the add is still seen.
TEST(SleepFolderMarkers, TimestampCatchesAddsThatReuseFreedSlots) {
  EXPECT_TRUE(folderMarkersChanged(/*tailNow=*/1200, /*stampNow=*/0x5A3C2222, /*tailSaved=*/1200,
                                   /*stampSaved=*/0x5A3C1111));
}

// A driver that does not stamp directories reports 0. That must read as "no
// timestamp available", not as a change, or every boot would pay a full folder
// walk forever.
TEST(SleepFolderMarkers, MissingTimestampIsNotAChange) {
  EXPECT_FALSE(folderMarkersChanged(/*tailNow=*/1200, /*stampNow=*/0, /*tailSaved=*/1200, /*stampSaved=*/0x5A3C1111));
}

// Markers saved before this signal existed carry no timestamp. Counting that as
// a change costs one walk, which restamps them; ignoring it would leave the
// signal permanently inert on every card upgraded from an older build.
TEST(SleepFolderMarkers, MarkersWithoutASavedTimestampAreRestamped) {
  EXPECT_TRUE(folderMarkersChanged(/*tailNow=*/1200, /*stampNow=*/0x5A3C1111, /*tailSaved=*/1200, /*stampSaved=*/0));
}

// An unavailable timestamp must not mask a tail move.
TEST(SleepFolderMarkers, MissingTimestampStillHonoursTheTail) {
  EXPECT_TRUE(folderMarkersChanged(/*tailNow=*/1500, /*stampNow=*/0, /*tailSaved=*/1200, /*stampSaved=*/0));
}
