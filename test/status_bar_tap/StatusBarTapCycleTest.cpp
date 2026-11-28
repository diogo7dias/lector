#include <gtest/gtest.h>

#include "util/StatusBarTapCycle.h"

using namespace status_bar_tap;

TEST(StatusBarTapCycle, CyclesFractionLeftBookPercentHiddenAndBack) {
  State s{1, 0};  // Page N/M

  auto step = nextState(s);
  EXPECT_EQ(step.next.enabled, 1);
  EXPECT_EQ(step.next.pageFormat, 1);  // N left
  EXPECT_FALSE(step.layoutChanged);

  step = nextState(step.next);
  EXPECT_EQ(step.next.enabled, 1);
  EXPECT_EQ(step.next.pageFormat, 2);  // Book %
  EXPECT_FALSE(step.layoutChanged);

  step = nextState(step.next);
  EXPECT_EQ(step.next.enabled, 0);  // Hidden
  EXPECT_EQ(step.next.pageFormat, 0);
  EXPECT_TRUE(step.layoutChanged);

  step = nextState(step.next);
  EXPECT_EQ(step.next.enabled, 1);  // Page N/M
  EXPECT_EQ(step.next.pageFormat, 0);
  EXPECT_TRUE(step.layoutChanged);
}

TEST(StatusBarTapCycle, HiddenWithAnyStoredFormatRestoresToFraction) {
  const auto step = nextState(State{0, 2});
  EXPECT_EQ(step.next.enabled, 1);
  EXPECT_EQ(step.next.pageFormat, 0);
  EXPECT_TRUE(step.layoutChanged);
}

TEST(StatusBarTapCycle, HitTestsActiveTopAndBottomBandsOnly) {
  // 800 px tall screen, 10 px bezel insets, 19 px top bar, 26 px bottom bar.
  EXPECT_TRUE(hitTestBand(800, 10, 10, 19, 26, true, 770));
  EXPECT_TRUE(hitTestBand(800, 10, 10, 19, 26, true, 25));
  EXPECT_FALSE(hitTestBand(800, 10, 10, 19, 26, true, 400));
  EXPECT_FALSE(hitTestBand(800, 10, 10, 19, 26, true, 100));
  EXPECT_FALSE(hitTestBand(800, 10, 10, 19, 26, true, 700));
}

TEST(StatusBarTapCycle, TopOnlyBarDoesNotCaptureBottomEdge) {
  EXPECT_TRUE(hitTestBand(800, 0, 0, 26, 0, true, 20));
  EXPECT_FALSE(hitTestBand(800, 0, 0, 26, 0, true, 780));
}

TEST(StatusBarTapCycle, HiddenBarAcceptsBottomRecoveryTapOnly) {
  EXPECT_TRUE(hitTestBand(800, 10, 10, 0, 0, false, 780));
  EXPECT_FALSE(hitTestBand(800, 10, 10, 0, 0, false, 20));
  EXPECT_FALSE(hitTestBand(800, 10, 10, 0, 0, false, 400));
}

TEST(StatusBarTapCycle, RejectsOutOfBoundsCoordinates) {
  EXPECT_FALSE(hitTestBand(800, 0, 0, 20, 20, true, -1));
  EXPECT_FALSE(hitTestBand(800, 0, 0, 20, 20, true, 800));
  EXPECT_FALSE(hitTestBand(0, 0, 0, 20, 20, true, 0));
}
