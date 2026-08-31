#include <gtest/gtest.h>

#include "components/ComparisonLayout.h"

namespace {

constexpr int kBodyY = 60;
constexpr int kBodyHeight = 600;
constexpr int kHeadline = 28;
constexpr int kLine = 20;
constexpr int kGap = 6;

comparison_layout::Metrics metrics() {
  return comparison_layout::Metrics{kHeadline, /*labelHeight=*/kHeadline, kLine, kGap};
}

comparison_layout::Content bothSides(const int theirs, const int mine, const bool headline = true,
                                     const bool relation = false) {
  comparison_layout::Content content;
  content.hasHeadline = headline;
  content.sideLines[0] = theirs;
  content.sideLines[1] = mine;
  content.hasRelation = relation;
  return content;
}

}  // namespace

TEST(ComparisonSideHeight, ALabelAloneIsOneLineTall) {
  EXPECT_EQ(comparison_layout::sideHeightFor(metrics(), 0), kHeadline);
}

TEST(ComparisonSideHeight, EveryDetailLineAddsItselfAndAGap) {
  EXPECT_EQ(comparison_layout::sideHeightFor(metrics(), 1), kHeadline + kGap + kLine);
  EXPECT_EQ(comparison_layout::sideHeightFor(metrics(), 3), kHeadline + 3 * (kGap + kLine));
}

TEST(ComparisonHeight, TheTwoSidesAreSeparatedByADoubleGap) {
  const int side = comparison_layout::sideHeightFor(metrics(), 2);
  EXPECT_EQ(comparison_layout::heightFor(metrics(), bothSides(2, 2, /*headline=*/false)), side * 2 + kGap * 2);
}

TEST(ComparisonHeight, TheHeadlineIsSeparatedFromTheFirstSideToo) {
  const int withHeadline = comparison_layout::heightFor(metrics(), bothSides(2, 2));
  const int without = comparison_layout::heightFor(metrics(), bothSides(2, 2, /*headline=*/false));
  EXPECT_EQ(withHeadline - without, kHeadline + kGap * 2);
}

TEST(ComparisonHeight, TheRelationLineCostsADoubleGapAndOneLine) {
  const int with = comparison_layout::heightFor(metrics(), bothSides(2, 2, true, /*relation=*/true));
  const int without = comparison_layout::heightFor(metrics(), bothSides(2, 2));
  EXPECT_EQ(with - without, kGap * 2 + kLine);
}

TEST(ComparisonHeight, ASideWithFewerLinesIsShorter) {
  EXPECT_LT(comparison_layout::heightFor(metrics(), bothSides(2, 2)),
            comparison_layout::heightFor(metrics(), bothSides(3, 2)));
}

TEST(ComparisonTop, TheBlockIsCentredInTheBand) {
  const comparison_layout::Content content = bothSides(3, 2, true, true);
  const int height = comparison_layout::heightFor(metrics(), content);
  EXPECT_EQ(comparison_layout::topFor(metrics(), kBodyY, kBodyHeight, content),
            kBodyY + (kBodyHeight - height) / 2);
}

TEST(ComparisonTop, TheHeadlineIsNeverPushedAboveTheBand) {
  // The choices take the foot of the body, so the band left for the comparison
  // can be shorter than the comparison itself.
  const comparison_layout::Content content = bothSides(3, 3, true, true);
  EXPECT_EQ(comparison_layout::topFor(metrics(), kBodyY, /*bandHeight=*/40, content), kBodyY);
}

TEST(ComparisonTop, ATallerBlockStartsHigher) {
  EXPECT_GT(comparison_layout::topFor(metrics(), kBodyY, kBodyHeight, bothSides(1, 1)),
            comparison_layout::topFor(metrics(), kBodyY, kBodyHeight, bothSides(3, 3)));
}

TEST(ComparisonHeight, AnEmptyComparisonIsJustTheTwoLabels) {
  comparison_layout::Content content;
  content.hasHeadline = false;
  EXPECT_EQ(comparison_layout::heightFor(metrics(), content), kHeadline * 2 + kGap * 2);
}
