#include <gtest/gtest.h>

#include <vector>

#include "activities/home/LargeFolderIndexPolicy.h"
#include "activities/home/VisibleEntryMap.h"

namespace {
std::vector<size_t> sourceIndexes(const VisibleEntryMap& map) {
  std::vector<size_t> indexes;
  for (size_t visibleIndex = 0; visibleIndex < map.count(); ++visibleIndex) {
    size_t sourceIndex = 0;
    EXPECT_TRUE(map.sourceIndexAt(visibleIndex, sourceIndex));
    indexes.push_back(sourceIndex);
  }
  return indexes;
}
}  // namespace

TEST(VisibleEntryMap, StartsAsIdentityView) {
  VisibleEntryMap map;

  ASSERT_TRUE(map.reset(5));

  EXPECT_EQ(map.count(), 5U);
  EXPECT_EQ(sourceIndexes(map), (std::vector<size_t>{0, 1, 2, 3, 4}));
}

TEST(VisibleEntryMap, RepeatedEraseUsesCurrentVisibleCoordinates) {
  VisibleEntryMap map;
  ASSERT_TRUE(map.reset(6));

  ASSERT_TRUE(map.eraseAt(1));
  EXPECT_EQ(sourceIndexes(map), (std::vector<size_t>{0, 2, 3, 4, 5}));

  ASSERT_TRUE(map.eraseAt(1));
  EXPECT_EQ(sourceIndexes(map), (std::vector<size_t>{0, 3, 4, 5}));

  ASSERT_TRUE(map.eraseAt(2));
  EXPECT_EQ(sourceIndexes(map), (std::vector<size_t>{0, 3, 5}));
}

TEST(VisibleEntryMap, RejectsIndexesAndCountsOutsideBounds) {
  VisibleEntryMap map;
  size_t sourceIndex = 0;

  EXPECT_FALSE(map.sourceIndexAt(0, sourceIndex));
  EXPECT_FALSE(map.eraseAt(0));
  EXPECT_FALSE(map.reset(large_folder_index::MAX_INDEX_ENTRIES + 1));
  EXPECT_EQ(map.count(), 0U);

  ASSERT_TRUE(map.reset(2));
  EXPECT_FALSE(map.sourceIndexAt(2, sourceIndex));
  EXPECT_FALSE(map.eraseAt(2));
  EXPECT_EQ(map.count(), 2U);
}

TEST(VisibleEntryMap, ResetRestoresIdentityAfterRemovals) {
  VisibleEntryMap map;
  ASSERT_TRUE(map.reset(5));
  ASSERT_TRUE(map.eraseAt(0));
  ASSERT_TRUE(map.eraseAt(2));

  ASSERT_TRUE(map.reset(3));

  EXPECT_EQ(map.count(), 3U);
  EXPECT_EQ(sourceIndexes(map), (std::vector<size_t>{0, 1, 2}));
}
