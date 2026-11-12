#include <gtest/gtest.h>

#include <optional>

#include "AnchorPlacement.h"

namespace {
std::optional<uint16_t> pageOf(const AnchorPlacement& anchors, const uint64_t id) {
  for (const auto& [anchorId, page] : anchors.placed()) {
    if (anchorId == id) return page;
  }
  return std::nullopt;
}
}  // namespace

TEST(AnchorPlacement, BlockTargetLandsOnThePageOfItsFirstLine) {
  AnchorPlacement anchors;
  anchors.waitForContent(1);
  EXPECT_EQ(pageOf(anchors, 1), std::nullopt);
  anchors.lineAdded(4);
  EXPECT_EQ(pageOf(anchors, 1), 4);
}

TEST(AnchorPlacement, BlockTargetFollowsItsFirstLineWhenCarried) {
  AnchorPlacement anchors;
  anchors.waitForContent(1);
  anchors.lineAdded(2);
  anchors.lineAdded(2);
  anchors.linesCarried(2, 3);
  EXPECT_EQ(pageOf(anchors, 1), 3);
}

TEST(AnchorPlacement, ACarryThatLeavesTheFirstLineBehindDoesNotMoveTheTarget) {
  AnchorPlacement anchors;
  anchors.waitForContent(1);
  anchors.lineAdded(2);
  anchors.lineAdded(2);
  anchors.linesCarried(1, 3);
  EXPECT_EQ(pageOf(anchors, 1), 2);
}

TEST(AnchorPlacement, ImageOrRuleCountsAsContent) {
  AnchorPlacement anchors;
  anchors.waitForContent(1);
  anchors.contentPlaced(5);
  EXPECT_EQ(pageOf(anchors, 1), 5);
}

TEST(AnchorPlacement, InlineTargetLandsOnTheLineHoldingItsOffset) {
  AnchorPlacement anchors;
  anchors.waitForLine(7, 150);
  anchors.lineStarting(0, 1);
  anchors.lineAdded(1);
  anchors.lineStarting(100, 1);
  anchors.lineAdded(1);
  EXPECT_EQ(pageOf(anchors, 7), std::nullopt);
  anchors.lineStarting(200, 2);
  anchors.lineAdded(2);
  EXPECT_EQ(pageOf(anchors, 7), 2);
}

TEST(AnchorPlacement, InlineTargetFollowsACarriedLine) {
  AnchorPlacement anchors;
  anchors.waitForLine(7, 50);
  anchors.lineStarting(0, 1);
  anchors.lineAdded(1);
  anchors.lineStarting(100, 1);
  anchors.linesCarried(1, 2);
  anchors.lineAdded(2);
  EXPECT_EQ(pageOf(anchors, 7), 2);
}

TEST(AnchorPlacement, BlockEndPlacesTargetsOnTheLastLine) {
  AnchorPlacement anchors;
  anchors.waitForLine(7, 50);
  anchors.lineStarting(0, 3);
  anchors.lineAdded(3);
  anchors.blockEnded(3);
  anchors.linesCarried(1, 4);
  EXPECT_EQ(pageOf(anchors, 7), 4);
}

TEST(AnchorPlacement, ABlockThatPlacedNothingLandsOnTheLastPage) {
  AnchorPlacement anchors;
  anchors.waitForContent(1);
  anchors.waitForLine(2, 10);
  anchors.finish(9);
  EXPECT_EQ(pageOf(anchors, 1), 9);
  EXPECT_EQ(pageOf(anchors, 2), 9);
}

TEST(AnchorPlacement, CountIncludesTargetsStillWaiting) {
  AnchorPlacement anchors;
  anchors.place(1, 0);
  anchors.waitForContent(2);
  anchors.waitForLine(3, 0);
  EXPECT_EQ(anchors.count(), 3u);
}
