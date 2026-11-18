#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "FirmwareBoardTag.h"

namespace {

bool mismatches(const std::string& image, const size_t chunk = 4096) {
  board_tag::Scanner scanner;
  for (size_t i = 0; i < image.size(); i += chunk) {
    scanner.feed(reinterpret_cast<const uint8_t*>(image.data()) + i, std::min(chunk, image.size() - i));
  }
  return scanner.mismatch();
}

TEST(FirmwareBoardTag, TheRunningBoardsOwnTagPasses) {
  EXPECT_EQ(std::string(board_tag::boardName(), board_tag::boardNameLen()), "x4pro");
  EXPECT_FALSE(mismatches(std::string(100, 'a') + board_tag::TAG + std::string(100, 'b')));
}

TEST(FirmwareBoardTag, AnImageWithNoTagPasses) { EXPECT_FALSE(mismatches(std::string(5000, 'z'))); }

TEST(FirmwareBoardTag, AnotherBoardsTagIsRefused) {
  board_tag::Scanner scanner;
  const std::string image = "..CROSSPOINT-BOARD-V1:x4;..";
  scanner.feed(reinterpret_cast<const uint8_t*>(image.data()), image.size());
  EXPECT_TRUE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "x4");
}

TEST(FirmwareBoardTag, ANameThatOnlyStartsLikeOursIsRefused) {
  EXPECT_TRUE(mismatches("CROSSPOINT-BOARD-V1:x4pro2;"));
  EXPECT_TRUE(mismatches("CROSSPOINT-BOARD-V1:x4pr;"));
}

TEST(FirmwareBoardTag, ATagSplitOverEveryChunkSizeIsStillFound) {
  const std::string image = std::string(37, 'q') + "CROSSPOINT-BOARD-V1:papermono;" + std::string(11, 'q');
  for (size_t chunk = 1; chunk < 40; ++chunk) EXPECT_TRUE(mismatches(image, chunk)) << chunk;
}

TEST(FirmwareBoardTag, AFalseStartInsideTheMagicIsRecovered) {
  EXPECT_TRUE(mismatches("CCROSSPOINT-BOARD-V1:x4;"));
  EXPECT_TRUE(mismatches("CROSSPOINT-BOARD-CROSSPOINT-BOARD-V1:x4;"));
}

TEST(FirmwareBoardTag, AnOverlongOrBinaryNameIsAChanceCollisionNotATag) {
  EXPECT_FALSE(mismatches(std::string("CROSSPOINT-BOARD-V1:") + std::string(40, 'a') + ";"));
  EXPECT_FALSE(mismatches(std::string("CROSSPOINT-BOARD-V1:\x01\x02;")));
}

}  // namespace
