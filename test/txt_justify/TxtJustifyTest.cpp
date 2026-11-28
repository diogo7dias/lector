#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "TxtJustifyLine.h"

namespace {

struct DrawnWord {
  int x;
  std::string text;
};

int fixedMeasure(const char* word) { return static_cast<int>(std::strlen(word)) * 10; }

TEST(TxtJustify, DistributesSpareSpaceFlushToContentWidth) {
  std::vector<DrawnWord> words;
  // "one" (30) + "two" (30) + "four" (40) = 100 px; 2 natural gaps of 5 = 10 px; total = 110 px.
  // With contentWidth = 123, spare = 13 -> gaps get +7 and +6 (total gap widths 12 and 11).
  const bool ok = txt_justify::layoutWords("one two four", 123, 5, fixedMeasure,
                                           [&](const int x, const char* w) { words.push_back({x, w}); });
  ASSERT_TRUE(ok);
  ASSERT_EQ(words.size(), 3u);
  EXPECT_EQ(words[0].x, 0);
  EXPECT_EQ(words[0].text, "one");
  EXPECT_EQ(words[1].x, 30 + 5 + 7);
  EXPECT_EQ(words[1].text, "two");
  EXPECT_EQ(words[2].x, 30 + 5 + 7 + 30 + 5 + 6);
  EXPECT_EQ(words[2].text, "four");
  EXPECT_EQ(words.back().x + fixedMeasure(words.back().text.c_str()), 123);
}

TEST(TxtJustify, SingleWordOrAlreadyFullReturnsFalse) {
  int calls = 0;
  EXPECT_FALSE(txt_justify::layoutWords("singleword", 200, 5, fixedMeasure, [&](int, const char*) { ++calls; }));
  EXPECT_EQ(calls, 0);
  // "one" (30) + 5 + "two" (30) = 65 >= 65 -> no spare space.
  EXPECT_FALSE(txt_justify::layoutWords("one two", 65, 5, fixedMeasure, [&](int, const char*) { ++calls; }));
  EXPECT_EQ(calls, 0);
}

TEST(TxtJustify, PreservesLeadingIndentWhileJustifyingInterWordGaps) {
  std::vector<DrawnWord> words;
  // 2 leading spaces (10 px) + "aa" (20) + 5 + "bb" (20) = 55 px; width 75 -> spare 20.
  const bool ok = txt_justify::layoutWords("  aa bb", 75, 5, fixedMeasure,
                                           [&](const int x, const char* w) { words.push_back({x, w}); });
  ASSERT_TRUE(ok);
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].x, 10);
  EXPECT_EQ(words[1].x, 10 + 20 + 5 + 20);
  EXPECT_EQ(words[1].x + 20, 75);
}

}  // namespace
