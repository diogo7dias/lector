#include <gtest/gtest.h>

#include "components/StatusStack.h"

namespace {

constexpr int kBodyY = 60;
constexpr int kBodyHeight = 600;

status_stack::Metrics metrics(const int progressHeight = 0) {
  return status_stack::Metrics{/*headlineHeight=*/28, /*lineHeight=*/20, /*gap=*/6, progressHeight};
}

int centreOf(const int height) { return kBodyY + (kBodyHeight - height) / 2; }

}  // namespace

TEST(StatusStackHeight, AnEmptyScreenTakesNoRoom) { EXPECT_EQ(status_stack::heightFor(metrics(), 0, false), 0); }

TEST(StatusStackHeight, OneLineIsTheHeadlineAlone) { EXPECT_EQ(status_stack::heightFor(metrics(), 1, false), 28); }

TEST(StatusStackHeight, EveryLineAfterTheHeadlineCarriesItsGap) {
  EXPECT_EQ(status_stack::heightFor(metrics(), 2, false), 28 + 6 + 20);
  EXPECT_EQ(status_stack::heightFor(metrics(), 4, false), 28 + (6 + 20) * 3);
}

TEST(StatusStackHeight, TheBarAddsItsDoubleGap) {
  EXPECT_EQ(status_stack::heightFor(metrics(6), 1, true), 28 + 12 + 6);
}

TEST(StatusStackHeight, ABarWithNoLinesStillMeasures) {
  EXPECT_EQ(status_stack::heightFor(metrics(6), 0, true), 12 + 6);
}

TEST(StatusStackTop, TheBlockIsCentredInTheBody) {
  const int height = status_stack::heightFor(metrics(), 2, false);
  EXPECT_EQ(status_stack::topFor(metrics(), kBodyY, kBodyHeight, 2, false), centreOf(height));
}

TEST(StatusStackTop, TwoLinesAndFourLinesShareTheSameMiddle) {
  const auto middle = [](const int lines) {
    return status_stack::topFor(metrics(), kBodyY, kBodyHeight, lines, false) +
           status_stack::heightFor(metrics(), lines, false) / 2;
  };
  EXPECT_NEAR(middle(2), middle(4), 1);
}

TEST(StatusStackTop, ABlockTallerThanTheBodyStartsAtTheTop) {
  // A short body: the four-line block cannot fit, and centring it would push the
  // headline off the top of the screen.
  EXPECT_EQ(status_stack::topFor(metrics(), kBodyY, /*bodyHeight=*/40, 4, false), kBodyY);
}
