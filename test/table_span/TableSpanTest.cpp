#include <gtest/gtest.h>

#include "Epub/parsers/TableSpan.h"

// The table layout treats any span greater than 1 as "this row cannot be a grid row", and it
// counts a rowspan down over the rows below it, so both the sentinel and the clamp matter.

TEST(TableSpan, AMissingAttributeIsNoSpan) {
  EXPECT_EQ(parseTableSpan(nullptr), 1);
  EXPECT_EQ(parseTableSpan(""), 1);
}

TEST(TableSpan, ReadsAPlainNumber) {
  EXPECT_EQ(parseTableSpan("1"), 1);
  EXPECT_EQ(parseTableSpan("2"), 2);
  EXPECT_EQ(parseTableSpan("17"), 17);
}

TEST(TableSpan, ZeroMeansToTheEndOfTheTableGroup) {
  EXPECT_EQ(parseTableSpan("0"), UINT16_MAX);
  EXPECT_EQ(parseTableSpan("00"), UINT16_MAX);
}

TEST(TableSpan, AnOutOfRangeNumberSaturatesInsteadOfWrapping) {
  EXPECT_EQ(parseTableSpan("65535"), UINT16_MAX);
  EXPECT_EQ(parseTableSpan("65536"), UINT16_MAX);
  EXPECT_EQ(parseTableSpan("999999999999"), UINT16_MAX);
}

TEST(TableSpan, MalformedValuesAreIgnoredRatherThanGuessed) {
  EXPECT_EQ(parseTableSpan("abc"), 1);
  EXPECT_EQ(parseTableSpan("2x"), 1);
  EXPECT_EQ(parseTableSpan(" 2"), 1);
  EXPECT_EQ(parseTableSpan("-2"), 1);
  EXPECT_EQ(parseTableSpan("2.5"), 1);
}
