#include <gtest/gtest.h>

#include "components/StatusStack.h"

namespace {

constexpr int kBodyY = 60;
constexpr int kBodyHeight = 600;

status_stack::Metrics metrics(const int progressHeight = 0) {
  return status_stack::Metrics{/*headlineHeight=*/28, /*lineHeight=*/20, /*gap=*/6, progressHeight};
}

int centreOf(const int height) { return kBodyY + (kBodyHeight - height) / 2; }

status_stack::Content lines(const int count, const bool showProgress = false) {
  status_stack::Content content;
  content.lineCount = count;
  content.showProgress = showProgress;
  return content;
}

}  // namespace

TEST(StatusStackHeight, AnEmptyScreenTakesNoRoom) { EXPECT_EQ(status_stack::heightFor(metrics(), lines(0)), 0); }

TEST(StatusStackHeight, OneLineIsTheHeadlineAlone) { EXPECT_EQ(status_stack::heightFor(metrics(), lines(1)), 28); }

TEST(StatusStackHeight, EveryLineAfterTheHeadlineCarriesItsGap) {
  EXPECT_EQ(status_stack::heightFor(metrics(), lines(2)), 28 + 6 + 20);
  EXPECT_EQ(status_stack::heightFor(metrics(), lines(4)), 28 + (6 + 20) * 3);
}

TEST(StatusStackHeight, TheBarAddsItsDoubleGap) {
  EXPECT_EQ(status_stack::heightFor(metrics(6), lines(1, true)), 28 + 12 + 6);
}

TEST(StatusStackHeight, ABarWithNoLinesStillMeasures) {
  EXPECT_EQ(status_stack::heightFor(metrics(6), lines(0, true)), 12 + 6);
}

TEST(StatusStackTop, TheBlockIsCentredInTheBody) {
  const int height = status_stack::heightFor(metrics(), lines(2));
  EXPECT_EQ(status_stack::topFor(metrics(), kBodyY, kBodyHeight, lines(2)), centreOf(height));
}

TEST(StatusStackTop, TwoLinesAndFourLinesShareTheSameMiddle) {
  const auto middle = [](const int count) {
    return status_stack::topFor(metrics(), kBodyY, kBodyHeight, lines(count)) +
           status_stack::heightFor(metrics(), lines(count)) / 2;
  };
  EXPECT_NEAR(middle(2), middle(4), 1);
}

TEST(StatusStackTop, ABlockTallerThanTheBodyStartsAtTheTop) {
  // A short body: the four-line block cannot fit, and centring it would push the
  // headline off the top of the screen.
  EXPECT_EQ(status_stack::topFor(metrics(), kBodyY, /*bodyHeight=*/40, lines(4)), kBodyY);
}

TEST(StatusStackHeight, ACodeTakesItsSquareAndTheAirAroundIt) {
  status_stack::Content content;
  content.lineCount = 2;
  content.qrSize = 198;
  const int withoutCode = status_stack::heightFor(metrics(), lines(2));
  EXPECT_EQ(status_stack::heightFor(metrics(), content), withoutCode + 12 + 198);
}

TEST(StatusStackHeight, TheAddressUnderTheCodeIsCountedToo) {
  status_stack::Content content;
  content.qrSize = 198;
  content.qrLineCount = 2;
  EXPECT_EQ(status_stack::heightFor(metrics(), content), 198 + (6 + 20) * 2);
}

TEST(StatusStackHeight, ACodeOnItsOwnNeedsNoAirAboveIt) {
  status_stack::Content content;
  content.qrSize = 198;
  EXPECT_EQ(status_stack::heightFor(metrics(), content), 198);
}

TEST(StatusStackTop, ABlockWithACodeIsStillCentred) {
  status_stack::Content content;
  content.lineCount = 2;
  content.qrSize = 198;
  content.qrLineCount = 2;
  EXPECT_EQ(status_stack::topFor(metrics(), kBodyY, kBodyHeight, content),
            centreOf(status_stack::heightFor(metrics(), content)));
}
