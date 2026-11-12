#include <HalStorage.h>
#include <gtest/gtest.h>

#include "KOReaderDocumentId.h"

class KOReaderDocumentIdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    HalFile::fileLength = 4096;
    HalFile::seekFails = false;
    HalFile::readResult = -2;
  }
};

TEST_F(KOReaderDocumentIdTest, AWholeReadProducesAnId) {
  EXPECT_FALSE(KOReaderDocumentId::calculate("/a.epub").empty());
}

TEST_F(KOReaderDocumentIdTest, AFailedSeekProducesNoId) {
  HalFile::seekFails = true;
  EXPECT_TRUE(KOReaderDocumentId::calculate("/a.epub").empty());
}

TEST_F(KOReaderDocumentIdTest, AShortReadProducesNoId) {
  HalFile::readResult = 10;
  EXPECT_TRUE(KOReaderDocumentId::calculate("/a.epub").empty());
}

TEST_F(KOReaderDocumentIdTest, AReadErrorProducesNoId) {
  HalFile::readResult = -1;
  EXPECT_TRUE(KOReaderDocumentId::calculate("/a.epub").empty());
}
