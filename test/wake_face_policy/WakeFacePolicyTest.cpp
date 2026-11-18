#include <gtest/gtest.h>

#include "sleep/WakeFacePolicy.h"

TEST(WakeFacePolicy, EveryOfferedFaceKeepsItsStoredNumber) {
  // CrossPoint's numbers: Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume,
  // Transparent, Cover + Transparent. A 0.58.0 file only ever held 2 or 3.
  for (uint8_t mode = 0; mode <= 8; mode++) EXPECT_EQ(wake_face::migrateSleepScreen(mode), mode);
}

TEST(WakeFacePolicy, ValuesNoBuildOffersBecomeCustom) {
  EXPECT_EQ(wake_face::migrateSleepScreen(9), 2);   // lector's old Transparent slot
  EXPECT_EQ(wake_face::migrateSleepScreen(42), 2);  // anything a future build wrote
  EXPECT_EQ(wake_face::migrateSleepScreen(255), 2);
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
