// The Wi-Fi row marks share one slot: chip, padlock and bars must each stay inside
// the width the list holds back for them, and must never touch each other.
#include <gtest/gtest.h>

#include "components/RowMarks.h"

namespace {

using row_marks::Rect;

bool inside(const Rect& inner, const Rect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

bool apart(const Rect& left, const Rect& right) { return left.x + left.width < right.x; }

constexpr int kSlotRight = 460;
constexpr int kCentre = 100;
constexpr int kChipText = 30;
constexpr int kLine = 16;

}  // namespace

TEST(RowMarks, EveryMarkFitsTheReservedSlotInOrder) {
  const int width = row_marks::slotWidth(kChipText);
  const Rect slot{kSlotRight - width, kCentre - 20, width, 40};
  const Rect chip = row_marks::chipRect(kSlotRight, kCentre, kChipText, kLine);
  const Rect lock = row_marks::lockRect(kSlotRight, kCentre);
  const Rect bars = row_marks::barsRect(kSlotRight, kCentre);

  EXPECT_TRUE(inside(chip, slot));
  EXPECT_TRUE(inside(lock, slot));
  EXPECT_TRUE(inside(bars, slot));
  EXPECT_EQ(chip.x, slot.x) << "the chip opens the slot, so the slot is no wider than it needs";
  EXPECT_EQ(bars.x + bars.width, kSlotRight);
  EXPECT_TRUE(apart(chip, lock));
  EXPECT_TRUE(apart(lock, bars));
}

TEST(RowMarks, ThePadlockIsDrawnInsideItsBox) {
  const Rect lock = row_marks::lockRect(kSlotRight, kCentre);
  for (int part = 0; part < row_marks::kLockParts; ++part) {
    EXPECT_TRUE(inside(row_marks::lockPart(lock, part), lock)) << "part " << part;
  }
  // The body sits under the shackle and spans the whole box.
  const Rect body = row_marks::lockPart(lock, 3);
  EXPECT_EQ(body.width, lock.width);
  EXPECT_EQ(body.y + body.height, lock.y + lock.height);
}

TEST(RowMarks, MarksAreCentredOnTheRow) {
  const Rect bars = row_marks::barsRect(kSlotRight, kCentre);
  const Rect lock = row_marks::lockRect(kSlotRight, kCentre);
  EXPECT_EQ(bars.y, lock.y);
  EXPECT_LE(bars.y, kCentre);
  EXPECT_GE(bars.y + bars.height, kCentre);
}
