#include <gtest/gtest.h>

#include "ReaderProgressSaveDebouncer.h"

TEST(ReaderProgressSaveDebouncer, MetadataChangeQueuesSamePosition) {
  ReaderProgressSaveDebouncer debouncer;
  constexpr uint32_t position = (3U << 16) | 50U;

  EXPECT_FALSE(debouncer.observe(position, 100));
  debouncer.markPersisted(position, 100);
  EXPECT_FALSE(debouncer.hasPending());

  EXPECT_FALSE(debouncer.observe(position, 120));
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_EQ(debouncer.lastObservedPosition(), position);
  EXPECT_EQ(debouncer.lastObservedMetadata(), 120U);
}

TEST(ReaderProgressSaveDebouncer, StaleMetadataSaveDoesNotClearPendingState) {
  ReaderProgressSaveDebouncer debouncer;
  constexpr uint32_t position = (3U << 16) | 50U;

  debouncer.observe(position, 100);
  debouncer.markPersisted(position, 100);
  debouncer.observe(position, 120);

  debouncer.markPersisted(position, 100);
  EXPECT_TRUE(debouncer.hasPending());

  debouncer.markPersisted(position, 120);
  EXPECT_FALSE(debouncer.hasPending());
}

TEST(ReaderProgressSaveDebouncer, MetadataChangeDoesNotCountAsPageTurn) {
  ReaderProgressSaveDebouncer debouncer;
  constexpr uint32_t initialPosition = (3U << 16) | 50U;

  debouncer.observe(initialPosition, 100);
  debouncer.markPersisted(initialPosition, 100);
  EXPECT_FALSE(debouncer.observe(initialPosition, 120));

  for (uint32_t page = 51; page < 60; ++page) {
    EXPECT_FALSE(debouncer.observe((3U << 16) | page, 120));
  }
  EXPECT_TRUE(debouncer.observe((3U << 16) | 60U, 120));
}

TEST(ReaderProgressSaveDebouncer, PositionOnlyCallersKeepExistingBehavior) {
  ReaderProgressSaveDebouncer debouncer;

  EXPECT_FALSE(debouncer.observe(7));
  debouncer.markPersisted(7);
  EXPECT_FALSE(debouncer.hasPending());
  EXPECT_FALSE(debouncer.observe(8));
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_EQ(debouncer.lastObservedMetadata(), 0U);
}

TEST(ReaderProgressSaveDebouncer, FirstPageIsNotATurnAndTenTurnsAreDue) {
  ReaderProgressSaveDebouncer debouncer;
  EXPECT_FALSE(debouncer.observe(0));
  for (uint32_t page = 1; page < 10; ++page) EXPECT_FALSE(debouncer.observe(page));
  EXPECT_TRUE(debouncer.observe(10));
  // A failed write leaves the batch due, including on a same-page retry.
  EXPECT_TRUE(debouncer.observe(10));
  debouncer.markPersisted(10);
  EXPECT_FALSE(debouncer.hasPending());
  EXPECT_FALSE(debouncer.observe(10));
}

TEST(ReaderProgressSaveDebouncer, TimeLimitIncludesSamePageAndClockWrap) {
  testMillis = UINT32_MAX - 1000;
  ReaderProgressSaveDebouncer debouncer;
  EXPECT_FALSE(debouncer.observe(12));
  testMillis += 299999;
  EXPECT_FALSE(debouncer.observe(12));
  ++testMillis;
  EXPECT_TRUE(debouncer.observe(12));
  debouncer.markPersisted(12);
  testMillis += 300000;
  EXPECT_FALSE(debouncer.observe(12));  // Clean state never writes just because time passed.
  testMillis = 0;
}

TEST(ReaderProgressSaveDebouncer, ExitFlushAndExplicitSaveClearTheBatchOnlyOnSuccess) {
  ReaderProgressSaveDebouncer debouncer;
  debouncer.observe(7, 20);
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_EQ(debouncer.lastObservedPosition(), 7U);
  EXPECT_EQ(debouncer.lastObservedMetadata(), 20U);
  debouncer.markPersisted(7, 20);  // The caller's successful flush or one-shot sync save.
  EXPECT_FALSE(debouncer.hasPending());
  debouncer.observe(8, 20);
  EXPECT_TRUE(debouncer.hasPending());
}
