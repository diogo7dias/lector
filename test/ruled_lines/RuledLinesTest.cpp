#include <gtest/gtest.h>

#include <set>
#include <utility>

#include "RuledLines.h"

namespace {
using Pixels = std::set<std::pair<int, int>>;

Pixels rule(const int step, const Pixels& ink, const int left = 0, const int width = 20, const int y = 10) {
  Pixels dots;
  ruled_lines::drawRow(
      left, width, y, step, [&](int x, int py) { return ink.count({x, py}) > 0; },
      [&](int x, int py) { dots.insert({x, py}); });
  return dots;
}
}  // namespace

TEST(RuledLines, StepPerShade) {
  EXPECT_EQ(ruled_lines::dotStep(ruled_lines::OFF), 0);
  EXPECT_EQ(ruled_lines::dotStep(ruled_lines::LIGHT), 4);
  EXPECT_EQ(ruled_lines::dotStep(ruled_lines::MEDIUM), 2);
  EXPECT_EQ(ruled_lines::dotStep(ruled_lines::DARK), 1);
  EXPECT_EQ(ruled_lines::dotStep(7), 0);
}

TEST(RuledLines, OffDrawsNothing) { EXPECT_TRUE(rule(0, {}).empty()); }

TEST(RuledLines, EmptyRowIsAFullDottedLine) {
  const Pixels dots = rule(4, {});
  EXPECT_EQ(dots, (Pixels{{0, 10}, {4, 10}, {8, 10}, {12, 10}, {16, 10}}));
}

TEST(RuledLines, BreaksAroundInkAndKeepsItsGrid) {
  // A descender pixel one row above the rule at x = 9.
  const Pixels dots = rule(1, {{9, 9}});
  for (int x = 0; x < 20; ++x) {
    const bool clear = x < 9 - ruled_lines::PADDING || x > 9 + ruled_lines::PADDING;
    EXPECT_EQ(dots.count({x, 10}) > 0, clear) << "x=" << x;
  }
  // Light: the dots after the gap stay on the multiples of 4.
  for (const auto& dot : rule(4, {{9, 9}})) EXPECT_EQ(dot.first % 4, 0);
}

TEST(RuledLines, InkOutsideTheBandIsIgnored) {
  EXPECT_EQ(rule(1, {{5, 10 - ruled_lines::PADDING - 1}, {6, 10 + ruled_lines::PADDING + 1}}).size(), 20u);
}

TEST(RuledLines, StaysInsideItsWidth) {
  for (const auto& dot : rule(1, {}, 5, 10)) {
    EXPECT_GE(dot.first, 5);
    EXPECT_LT(dot.first, 15);
  }
}
