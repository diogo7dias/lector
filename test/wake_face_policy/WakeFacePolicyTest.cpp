#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

TEST(WakeFacePolicy, StampedFilesKeepEveryOfferedFace) {
  // CrossPoint's numbers plus Lector's Quote face: Dark, Light, Custom, Cover, Cover + Custom,
  // None, Quick Resume, Transparent, Cover + Transparent, Quote.
  for (uint8_t mode = 0; mode <= 9; mode++) EXPECT_EQ(wake_face::sleepFaceFromFile(mode, true), mode);
  EXPECT_EQ(wake_face::sleepFaceFromFile(10, true), 2);  // anything a future build wrote
  EXPECT_EQ(wake_face::sleepFaceFromFile(42, true), 2);
  EXPECT_EQ(wake_face::sleepFaceFromFile(255, true), 2);
}

TEST(WakeFacePolicy, OlderLectorFilesKeepWhatTheirNumbersMeant) {
  // 0.58.0 wrote only 2 and 3.
  EXPECT_EQ(wake_face::sleepFaceFromFile(2, false), 2);
  EXPECT_EQ(wake_face::sleepFaceFromFile(3, false), 3);
  // Before the nine-face picker: 7 was the stats dashboard and 8 Freeze (both gone), and 9 the
  // transparent overlay, which is 7 now.
  EXPECT_EQ(wake_face::sleepFaceFromFile(7, false), 2);
  EXPECT_EQ(wake_face::sleepFaceFromFile(8, false), 2);
  EXPECT_EQ(wake_face::sleepFaceFromFile(9, false), 7);
  EXPECT_EQ(wake_face::sleepFaceFromFile(42, false), 2);
}

TEST(WakeFacePolicy, FastUnlockShortensOnlyTheLadderSettleWindow) {
  EXPECT_EQ(wake_face::inputSettleMs(false, true), 100ul);
  EXPECT_EQ(wake_face::inputSettleMs(false, false), 500ul);
  EXPECT_EQ(wake_face::inputSettleMs(true, true), 20ul);  // X4 Pro: digital inputs, no ladder
  EXPECT_EQ(wake_face::inputSettleMs(true, false), 20ul);
}

TEST(WakeFacePolicy, DriveAllWakeOnlyWithFastUnlock) {
  using wake_face::WakeClear;
  using wake_face::wakeClearFor;
  EXPECT_EQ(wakeClearFor(true), WakeClear::DriveAll);
  // Fast Unlock off restores the clearing pass.
  EXPECT_EQ(wakeClearFor(false), WakeClear::Blank);
}

TEST(WakeFacePolicy, X4ProTakesTheSameDriveAllPathAsTheX4) {
  // The X4 Pro (SSD1677 on early batches, UC8179 on later ones) has no firmware branch
  // of its own on this path: the board split lives in the panel driver, and both drivers
  // honour the same one-shot. The policy answer for a non-X3 board is the X4's.
  using wake_face::WakeClear;
  constexpr bool kIsX3 = false;  // an X4 Pro, either batch
  EXPECT_EQ(wake_face::wakeClearFor(true), WakeClear::DriveAll);
  EXPECT_TRUE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, kIsX3));
  EXPECT_EQ(wake_face::inputSettleMs(/*isX4Pro=*/true, true), 20ul);
}

TEST(WakeFacePolicy, FirstPageTurnCleansAfterADriveAllWakeExceptOnTheX3) {
  using wake_face::WakeClear;
  EXPECT_TRUE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, false));
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::DriveAll, true));  // X3 first paint was the scrub
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::Blank, false));    // the blank already cleaned
  EXPECT_FALSE(wake_face::firstPageTurnCleans(WakeClear::Blank, true));
}

TEST(WakeFacePolicy, QuickResumeOnTimeoutAppliesOnlyToTimeoutSleeps) {
  constexpr uint8_t kCustom = 2;
  constexpr uint8_t kQuickResume = 6;
  // Face = Quick Resume: both manual and timeout locks use Quick Resume regardless of toggle.
  EXPECT_TRUE(wake_face::usesQuickResumeForSleep(kQuickResume, 0, false));
  EXPECT_TRUE(wake_face::usesQuickResumeForSleep(kQuickResume, 0, true));
  EXPECT_TRUE(wake_face::quickResumePossible(kQuickResume, 0));
  // Face = Custom, timeout toggle off: neither lock uses Quick Resume.
  EXPECT_FALSE(wake_face::usesQuickResumeForSleep(kCustom, 0, false));
  EXPECT_FALSE(wake_face::usesQuickResumeForSleep(kCustom, 0, true));
  EXPECT_FALSE(wake_face::quickResumePossible(kCustom, 0));
  // Face = Custom, timeout toggle on: only timeout sleep uses Quick Resume, and wake may restore.
  EXPECT_FALSE(wake_face::usesQuickResumeForSleep(kCustom, 1, false));
  EXPECT_TRUE(wake_face::usesQuickResumeForSleep(kCustom, 1, true));
  EXPECT_TRUE(wake_face::quickResumePossible(kCustom, 1));
}
