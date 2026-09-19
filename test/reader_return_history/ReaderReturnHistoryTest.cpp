#include <gtest/gtest.h>

#include "activities/reader/ReaderReturnHistory.h"

namespace {
void expectReturn(ReaderReturnHistory& history, int32_t spine, uint32_t offset) {
  const auto target = history.beginReturn();
  ASSERT_TRUE(target);
  EXPECT_EQ(target->spineIndex, spine);
  EXPECT_EQ(target->contentOffset, offset);
}
}  // namespace

TEST(ReaderReturnHistory, EmptyHasNoReturn) {
  ReaderReturnHistory history;
  EXPECT_TRUE(history.empty());
  EXPECT_FALSE(history.beginReturn());
  history.finishReturn(true);
  EXPECT_TRUE(history.empty());
  static_assert(sizeof(ReaderReturnHistory::Position) == 8);
  static_assert(sizeof(ReaderReturnHistory) <= 68);  // 64-byte payload + bookkeeping/alignment
}

TEST(ReaderReturnHistory, SingleJumpKeepsFullWidthContentPosition) {
  ReaderReturnHistory history;
  history.recordJump({70000, 0xf1234567});
  EXPECT_FALSE(history.empty());
  expectReturn(history, 70000, 0xf1234567);
  EXPECT_FALSE(history.empty());  // initiating Return must not consume its destination
}

TEST(ReaderReturnHistory, SuccessfulReturnConsumesExactlyOneOrigin) {
  ReaderReturnHistory history;
  history.recordJump({2, 0});
  history.recordJump({5, 900});
  expectReturn(history, 5, 900);
  history.finishReturn(true);
  expectReturn(history, 2, 0);
  history.finishReturn(true);
  EXPECT_TRUE(history.empty());
}

TEST(ReaderReturnHistory, EightJumpsReturnNewestFirst) {
  ReaderReturnHistory history;
  for (int32_t i = 0; i < 8; ++i) history.recordJump({i, static_cast<uint32_t>(i * 100)});
  for (int32_t i = 7; i >= 0; --i) {
    expectReturn(history, i, i * 100);
    history.finishReturn(true);
  }
  EXPECT_TRUE(history.empty());
}

TEST(ReaderReturnHistory, NinthJumpEvictsOnlyOldest) {
  ReaderReturnHistory history;
  for (int32_t i = 0; i < 9; ++i) history.recordJump({i, static_cast<uint32_t>(i * 100)});
  for (int32_t i = 8; i >= 1; --i) {
    expectReturn(history, i, i * 100);
    history.finishReturn(true);
  }
  EXPECT_TRUE(history.empty());
}

TEST(ReaderReturnHistory, CancelledPickerRecordsNothingEvenWhenFull) {
  ReaderReturnHistory history;
  history.recordJump({99, 999}, false);
  EXPECT_TRUE(history.empty());
  for (int32_t i = 0; i < 8; ++i) history.recordJump({i, 0});
  history.recordJump({99, 999}, false);
  for (int32_t i = 7; i >= 0; --i) {
    expectReturn(history, i, 0);
    history.finishReturn(true);
  }
  EXPECT_TRUE(history.empty());
}

TEST(ReaderReturnHistory, FailedJumpDestinationPreservesOrigin) {
  ReaderReturnHistory history;
  history.recordJump({3, 450});  // accepted jump; destination then fails to load
  history.finishReturn(false);
  expectReturn(history, 3, 450);
  history.finishReturn(true);
  EXPECT_TRUE(history.empty());
}

TEST(ReaderReturnHistory, FailedReturnDestinationCanBeRetried) {
  ReaderReturnHistory history;
  history.recordJump({1, 200});
  history.recordJump({4, 800});
  expectReturn(history, 4, 800);
  history.finishReturn(false);
  expectReturn(history, 4, 800);
  history.finishReturn(true);
  expectReturn(history, 1, 200);
}

TEST(ReaderReturnHistory, OrdinaryPageLoadsDoNotRecordOrConsumeHistory) {
  ReaderReturnHistory history;
  for (int i = 0; i < 20; ++i) history.finishReturn(true);
  EXPECT_TRUE(history.empty());
  history.recordJump({2, 100});
  for (int i = 0; i < 20; ++i) history.finishReturn(true);
  expectReturn(history, 2, 100);
}

TEST(ReaderReturnHistory, NewJumpSupersedesPendingReturnAndReusesPoppedSlots) {
  ReaderReturnHistory history;
  for (int32_t i = 0; i < 9; ++i) history.recordJump({i, 0});
  expectReturn(history, 8, 0);
  history.finishReturn(true);
  expectReturn(history, 7, 0);
  history.recordJump({20, 123});  // accepted navigation replaces the in-flight Return
  history.finishReturn(true);     // loading its page must not consume the new origin
  expectReturn(history, 20, 123);
  history.finishReturn(true);
  for (int32_t i = 7; i >= 1; --i) {
    expectReturn(history, i, 0);
    history.finishReturn(true);
  }
  EXPECT_TRUE(history.empty());
}
