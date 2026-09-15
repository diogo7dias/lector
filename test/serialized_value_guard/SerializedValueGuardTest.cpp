#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "SerializedValueComparator.h"

TEST(SerializedValueGuard, ComparesAcrossChunksAndRejectsChangesAndShortReads) {
  char path[] = "/tmp/serialized-value-XXXXXX";
  const int fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  close(fd);
  const std::string content = std::string(130, 'x') + "\"escaped\\value\"";
  HalFile file;
  ASSERT_TRUE(file.open(path, "wb"));
  ASSERT_EQ(file.write(content.data(), content.size()), content.size());
  file.close();
  ASSERT_TRUE(file.open(path, "rb"));
  std::remove(path);
  SerializedValueComparator same(file);
  EXPECT_EQ(same.write(static_cast<uint8_t>(content[0])), 1U);
  EXPECT_EQ(same.write(reinterpret_cast<const uint8_t*>(content.data() + 1), content.size() - 1), content.size() - 1);
  EXPECT_TRUE(same.matches());
  EXPECT_EQ(same.write('!'), 0U);  // EOF cannot compare equal.
  EXPECT_FALSE(same.matches());
  EXPECT_EQ(same.write('!'), 0U);  // A failed comparison stays failed.
}

TEST(SerializedValueGuard, AChangedByteNeverMatches) {
  char path[] = "/tmp/serialized-value-XXXXXX";
  const int fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  close(fd);
  HalFile file;
  ASSERT_TRUE(file.open(path, "wb"));
  ASSERT_EQ(file.write('a'), 1U);
  file.close();
  ASSERT_TRUE(file.open(path, "rb"));
  std::remove(path);
  SerializedValueComparator changed(file);
  EXPECT_EQ(changed.write('b'), 0U);
  EXPECT_FALSE(changed.matches());
}

TEST(SerializedValueGuard, ReadFailureNeverMatches) {
  HalFile closed;
  SerializedValueComparator failed(closed);
  EXPECT_EQ(failed.write('a'), 0U);
  EXPECT_FALSE(failed.matches());
}
