#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "ReadingStats.h"
#include "ReadingStatsStore.h"

using namespace reading_stats;

TEST(ReadingStatsTracker, RejectsTooFastAndIdleForwardTurns) {
  ReadingStatsTracker tracker({.idleThresholdSeconds = 300, .minimumPageSeconds = 2, .minimumSessionSeconds = 60});
  tracker.startPage(1'000);

  EXPECT_FALSE(tracker.forwardTurn(2'500));
  tracker.startPage(3'000);
  EXPECT_TRUE(tracker.forwardTurn(5'000));
  tracker.startPage(6'000);
  EXPECT_FALSE(tracker.forwardTurn(307'000));

  EXPECT_EQ(tracker.session().readingSeconds, 2u);
  EXPECT_EQ(tracker.session().pagesTurned, 1u);
}

TEST(ReadingStatsTracker, CountsSessionOnlyAfterOneMinuteOfValidReading) {
  ReadingStatsTracker shortSession;
  shortSession.startPage(1'000);
  shortSession.pause(60'000);
  EXPECT_FALSE(shortSession.finish().countsAsSession);

  ReadingStatsTracker fullSession;
  fullSession.startPage(1'000);
  fullSession.pause(61'000);
  EXPECT_TRUE(fullSession.finish().countsAsSession);
  EXPECT_EQ(fullSession.session().readingSeconds, 60u);
}

TEST(ReadingStatsData, AppliesSessionWithSaturatingCounters) {
  ReadingStatsData data;
  data.totalReadingSeconds = std::numeric_limits<uint32_t>::max() - 2;
  data.totalPagesTurned = std::numeric_limits<uint32_t>::max();
  data.totalSessions = std::numeric_limits<uint32_t>::max();

  SessionResult result{.readingSeconds = 10, .pagesTurned = 3, .countsAsSession = true};
  data.apply(result);

  EXPECT_EQ(data.totalReadingSeconds, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(data.totalPagesTurned, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(data.totalSessions, std::numeric_limits<uint32_t>::max());
}

TEST(ReadingStatsCodec, RoundTripsEveryVersionOneField) {
  ReadingStatsData input;
  input.totalSessions = 12;
  input.totalReadingSeconds = 34'567;
  input.totalPagesTurned = 890;
  input.completedBooks = 4;
  input.averageSecondsPerPage = 42;
  input.paceSampleCount = 73;
  input.estimatedTimeLeftSeconds = 7'654;
  input.startDay = 20'001;
  input.finishedDay = 20'010;
  input.timeOfDaySeconds = {100, 200, 300, 400};
  input.dayOfWeekSeconds = {11, 22, 33, 44, 55, 66, 77};
  input.readingHistoryAnchorDay = 20'010;
  input.readingHistoryBits[0] = 0b10100101;
  input.longestReadingStreak = 8;
  input.completed = true;

  const auto bytes = ReadingStatsCodec::encode(input);
  ReadingStatsData output;
  ASSERT_EQ(ReadingStatsCodec::decode(bytes.data(), bytes.size(), output), DecodeResult::Ok);
  EXPECT_EQ(output, input);
}

TEST(ReadingStatsCodec, RejectsCorruptAndNewerFiles) {
  ReadingStatsData output;
  std::array<uint8_t, ReadingStatsCodec::encodedSize()> shortFile{};
  EXPECT_EQ(ReadingStatsCodec::decode(shortFile.data(), shortFile.size() - 1, output), DecodeResult::Invalid);

  auto newerFile = shortFile;
  newerFile[0] = ReadingStatsCodec::version() + 1;
  EXPECT_EQ(ReadingStatsCodec::decode(newerFile.data(), newerFile.size(), output), DecodeResult::NewerVersion);
}

TEST(ReadingStatsData, BuildsStableRunningPagePace) {
  ReadingStatsData data;
  data.recordForwardPage(30);
  data.recordForwardPage(60);
  data.recordForwardPage(45);

  EXPECT_EQ(data.averageSecondsPerPage, 45u);
  EXPECT_EQ(data.paceSampleCount, 3u);
}

TEST(ReadingStatsData, SplitsReadingAcrossTimeAndWeekdayBoundaries) {
  ReadingStatsData data;
  const LocalDateTime sundayNight{.dayIndex = 9'000, .dayOfWeek = 6, .hour = 20, .minute = 59, .second = 30};
  data.recordReadingSpan(sundayNight, 8 * 3600 + 60);

  EXPECT_EQ(data.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Evening)], 30u);
  EXPECT_EQ(data.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Night)], 8u * 3600u);
  EXPECT_EQ(data.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Morning)], 30u);
  EXPECT_EQ(data.dayOfWeekSeconds[6], 3u * 3600u + 30u);
  EXPECT_EQ(data.dayOfWeekSeconds[0], 5u * 3600u + 30u);
}

TEST(ReadingStatsData, TracksCurrentAndLongestReadingStreak) {
  ReadingStatsData data;
  data.markReadingDay(100);
  data.markReadingDay(101);
  data.markReadingDay(103);
  data.markReadingDay(104);
  data.markReadingDay(105);

  EXPECT_EQ(data.currentStreak(105), 3u);
  EXPECT_EQ(data.currentStreak(106), 3u);
  EXPECT_EQ(data.currentStreak(107), 0u);
  EXPECT_EQ(data.longestReadingStreak, 3u);
}

class MemoryStatsFiles final : public StatsFiles {
 public:
  std::unordered_map<std::string, std::vector<uint8_t>> files;

  bool read(const std::string& path, uint8_t* destination, size_t capacity, size_t& size) override {
    const auto found = files.find(path);
    if (found == files.end() || found->second.size() > capacity) return false;
    size = found->second.size();
    std::copy(found->second.begin(), found->second.end(), destination);
    return true;
  }

  bool write(const std::string& path, const uint8_t* source, size_t size) override {
    files[path] = std::vector<uint8_t>(source, source + size);
    return true;
  }

  bool exists(const std::string& path) const override { return files.contains(path); }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  bool rename(const std::string& from, const std::string& to) override {
    auto node = files.extract(from);
    if (node.empty()) return false;
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
};

TEST(ReadingStatsStore, SavesThroughTempAndRotatesValidBackup) {
  MemoryStatsFiles files;
  ReadingStatsStore store(files);
  ReadingStatsData first;
  first.totalSessions = 1;
  ASSERT_TRUE(store.save("/stats.bin", first));

  ReadingStatsData second;
  second.totalSessions = 2;
  ASSERT_TRUE(store.save("/stats.bin", second));

  EXPECT_FALSE(files.exists("/stats.bin.tmp"));
  EXPECT_TRUE(files.exists("/stats.bin.bak"));
  ReadingStatsData loaded;
  EXPECT_EQ(store.load("/stats.bin", loaded), StatsLoadResult::Ok);
  EXPECT_EQ(loaded.totalSessions, 2u);
}

TEST(ReadingStatsStore, RecoversFromBackupWhenMainIsCorrupt) {
  MemoryStatsFiles files;
  ReadingStatsStore store(files);
  ReadingStatsData expected;
  expected.totalReadingSeconds = 1234;
  const auto valid = ReadingStatsCodec::encode(expected);
  files.files["/stats.bin"] = {1, 2, 3};
  files.files["/stats.bin.bak"] = {valid.begin(), valid.end()};

  ReadingStatsData loaded;
  EXPECT_EQ(store.load("/stats.bin", loaded), StatsLoadResult::RecoveredBackup);
  EXPECT_EQ(loaded.totalReadingSeconds, 1234u);
}

TEST(ReadingStatsStore, NeverOverwritesNewerStatsFile) {
  MemoryStatsFiles files;
  ReadingStatsStore store(files);
  auto newer = ReadingStatsCodec::encode({});
  newer[0] = ReadingStatsCodec::version() + 1;
  files.files["/stats.bin"] = {newer.begin(), newer.end()};

  ReadingStatsData loaded;
  EXPECT_EQ(store.load("/stats.bin", loaded), StatsLoadResult::NewerVersion);
  EXPECT_FALSE(store.save("/stats.bin", {}));
  EXPECT_EQ(files.files["/stats.bin"][0], ReadingStatsCodec::version() + 1);
}
