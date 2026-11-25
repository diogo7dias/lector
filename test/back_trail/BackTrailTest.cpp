#include <gtest/gtest.h>

#include "activities/BackTrail.h"

using back_trail::pack;
using back_trail::Step;
using back_trail::unpack;

namespace {
constexpr uint8_t kMaxOpener = 7;   // ActivityManager::Opener::Reader
constexpr uint8_t kMaxHomeRow = 7;  // HomeMenuItem::QUICK_PANEL
}  // namespace

// Settings > File Transfer > USB Drive reboots: Back lands on the mode list (on USB Drive),
// and the mode list's Back on the Settings card.
TEST(BackTrail, ChainSurvivesTheWordInOrderWithRows) {
  const Step chain[] = {{0, 0}, {4, 4}};
  const auto trail = unpack(pack(chain, 2, 5), kMaxOpener, kMaxHomeRow);
  ASSERT_EQ(trail.count, 2);
  EXPECT_EQ(trail.steps[0].opener, 0);
  EXPECT_EQ(trail.steps[1].opener, 4);
  EXPECT_EQ(trail.steps[1].row, 4);
  EXPECT_EQ(trail.homeRow, 5);
}

// The OPDS browser opened from Home's row: no opener, Home on the OPDS row.
TEST(BackTrail, NoOpenerKeepsTheHomeRow) {
  const auto trail = unpack(pack(nullptr, 0, 4), kMaxOpener, kMaxHomeRow);
  EXPECT_EQ(trail.count, 0);
  EXPECT_EQ(trail.homeRow, 4);
}

// The very first opener is opener 0 on row 0: it must still read back as a step.
TEST(BackTrail, OpenerZeroOnRowZeroIsAStep) {
  const Step chain[] = {{0, 0}};
  EXPECT_EQ(unpack(pack(chain, 1, 0), kMaxOpener, kMaxHomeRow).count, 1);
}

TEST(BackTrail, DeepChainKeepsTheLastThree) {
  const Step chain[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};
  const auto trail = unpack(pack(chain, 4, 0), kMaxOpener, kMaxHomeRow);
  ASSERT_EQ(trail.count, 3);
  EXPECT_EQ(trail.steps[0].opener, 2);
  EXPECT_EQ(trail.steps[2].opener, 4);
}

TEST(BackTrail, RowClampsToFifteen) {
  const Step chain[] = {{5, 40}};
  EXPECT_EQ(unpack(pack(chain, 1, 0), kMaxOpener, kMaxHomeRow).steps[0].row, 15);
}

// RTC memory holds garbage on a cold boot; the magic word guards it, but a stray word still
// names nothing past the enums.
TEST(BackTrail, StrayWordNamesNothingPastTheEnums) {
  const auto trail = unpack(0xFFFFFFFFu, kMaxOpener, kMaxHomeRow);
  EXPECT_EQ(trail.count, 0);
  EXPECT_EQ(trail.homeRow, 0);
}

// The last opener (the book under Settings) must still fit its nibble.
TEST(BackTrail, HighestOpenerFitsItsNibble) {
  const Step chain[] = {{kMaxOpener, 0}, {0, 0}};
  const auto trail = unpack(pack(chain, 2, 0), kMaxOpener, kMaxHomeRow);
  ASSERT_EQ(trail.count, 2);
  EXPECT_EQ(trail.steps[0].opener, kMaxOpener);
}
