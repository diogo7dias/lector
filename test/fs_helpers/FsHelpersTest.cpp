#include <gtest/gtest.h>

#include <string_view>

#include "FsHelpers.h"

TEST(FsHelpers, RejectsEmptyAndDotComponents) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view(".")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("..")));
}

TEST(FsHelpers, RejectsPathSeparators) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("a/b")));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(std::string_view("a\\b")));
}

TEST(FsHelpers, AcceptsOrdinaryAndDottedNames) {
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("book.epub")));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("volume..2.epub")));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(std::string_view("notes...txt")));
}
