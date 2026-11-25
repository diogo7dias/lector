#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "sleep/SleepIndexRecord.h"

TEST(SleepIndexRecord, RoundTripsNameAndDirOffset) {
  char record[sleep_index_record::kRecordBytes];
  const char* name = "woodcut_night_watch_0042.bmp";
  const uint32_t offset = 0x00123460u;

  sleep_index_record::encode(record, name, std::strlen(name), offset);

  EXPECT_EQ(sleep_index_record::decodeName(record), name);
  EXPECT_EQ(sleep_index_record::decodeDirOffset(record), offset);
  // NUL padding between name and offset field.
  EXPECT_EQ(record[std::strlen(name)], '\0');
  EXPECT_EQ(record[sleep_index_record::kMaxNameBytes], '\0');
}

TEST(SleepIndexRecord, MaxLengthNameDoesNotClobberDirOffset) {
  char record[sleep_index_record::kRecordBytes];
  const std::string maxName(sleep_index_record::kMaxNameBytes, 'a');
  const uint32_t offset = 0xDEADBEEFu;

  sleep_index_record::encode(record, maxName.c_str(), maxName.size(), offset);

  EXPECT_EQ(sleep_index_record::decodeName(record), maxName);
  EXPECT_EQ(record[sleep_index_record::kMaxNameBytes], '\0');
  EXPECT_EQ(sleep_index_record::decodeDirOffset(record), offset);
}

TEST(SleepIndexRecord, OverlongNameIsTruncatedWithoutTouchingOffset) {
  char record[sleep_index_record::kRecordBytes];
  const std::string overlong(sleep_index_record::kMaxNameBytes + 20, 'z');
  const uint32_t offset = 4096u;

  sleep_index_record::encode(record, overlong.c_str(), overlong.size(), offset);

  EXPECT_EQ(sleep_index_record::decodeName(record).size(), sleep_index_record::kMaxNameBytes);
  EXPECT_EQ(sleep_index_record::decodeDirOffset(record), offset);
}

TEST(SleepIndexRecord, UnknownDirOffsetRoundTrips) {
  char record[sleep_index_record::kRecordBytes];
  sleep_index_record::encode(record, "a.bmp", 5, sleep_index_record::kUnknownDirOffset);
  EXPECT_EQ(sleep_index_record::decodeDirOffset(record), sleep_index_record::kUnknownDirOffset);
}

TEST(SleepIndexRecord, CandidateMatchingAcceptsExactAndCounterpartOnly) {
  EXPECT_TRUE(sleep_index_record::matchesCandidate("moon.bmp", "moon.bmp", "moon_F.bmp"));
  EXPECT_TRUE(sleep_index_record::matchesCandidate("moon_F.bmp", "moon.bmp", "moon_F.bmp"));
  EXPECT_FALSE(sleep_index_record::matchesCandidate("other.bmp", "moon.bmp", "moon_F.bmp"));
  EXPECT_FALSE(sleep_index_record::matchesCandidate("", "moon.bmp", "moon_F.bmp"));
  EXPECT_FALSE(sleep_index_record::matchesCandidate("moon_F.bmp", "moon.bmp", ""));
}
