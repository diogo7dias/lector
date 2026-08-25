#include <gtest/gtest.h>

#include "components/HeaderTitle.h"

namespace {

// The screen title is drawn in the same UI font as the rows under it, and Cozette ships
// no bold face, so the brackets are what sets the title apart from the list.
TEST(HeaderTitle, WrapsTheTitleInBrackets) {
  EXPECT_EQ(header_title::decorate("Settings"), "[Settings]");
  EXPECT_EQ(header_title::decorate("Text"), "[Text]");
}

TEST(HeaderTitle, LeavesAnEmptyTitleAlone) {
  EXPECT_EQ(header_title::decorate(""), "");
  EXPECT_EQ(header_title::decorate(nullptr), "");
}

// Idempotent: a caller that already bracketed its own title must not end up double-wrapped.
TEST(HeaderTitle, DoesNotBracketATitleThatAlreadyIs) {
  EXPECT_EQ(header_title::decorate("[Settings]"), "[Settings]");
}

}  // namespace
