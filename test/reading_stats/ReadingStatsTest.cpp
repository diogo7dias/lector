#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "ReaderStatsSession.h"
#include "ReadingStats.h"
#include "ReadingStatsPresentation.h"
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

TEST(ReadingStatsTracker, SamePageRedrawDoesNotRestartActiveTimer) {
  ReadingStatsTracker tracker;
  tracker.startPage(1'000);
  tracker.startPage(30'000);
  EXPECT_TRUE(tracker.forwardTurn(61'000));
  EXPECT_EQ(tracker.session().readingSeconds, 60u);
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

TEST(ReadingStatsCodec, RoundTripsEveryCurrentVersionField) {
  ReadingStatsData input;
  input.totalSessions = 12;
  input.resetEpoch = 9;
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
  input.completionCredited = true;

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

TEST(ReadingStatsCodec, MigratesLegacyVersionOneFiles) {
  std::array<uint8_t, 176> legacy{};
  legacy[0] = 1;
  legacy[1] = 1;
  const auto writeLegacy32 = [&legacy](const size_t offset, const uint32_t value) {
    legacy[offset] = static_cast<uint8_t>(value);
    legacy[offset + 1] = static_cast<uint8_t>(value >> 8);
    legacy[offset + 2] = static_cast<uint8_t>(value >> 16);
    legacy[offset + 3] = static_cast<uint8_t>(value >> 24);
  };
  writeLegacy32(2, 7);
  writeLegacy32(6, 3'600);
  writeLegacy32(10, 42);
  writeLegacy32(14, 3);

  ReadingStatsData migrated;
  ASSERT_EQ(ReadingStatsCodec::decode(legacy.data(), legacy.size(), migrated), DecodeResult::Ok);
  EXPECT_EQ(migrated.totalSessions, 7u);
  EXPECT_EQ(migrated.totalReadingSeconds, 3'600u);
  EXPECT_EQ(migrated.totalPagesTurned, 42u);
  EXPECT_EQ(migrated.completedBooks, 3u);
  EXPECT_TRUE(migrated.completed);
  EXPECT_TRUE(migrated.completionCredited);
  EXPECT_EQ(migrated.resetEpoch, 0u);
  EXPECT_EQ(ReadingStatsCodec::encode(migrated)[0], 2u);
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
  const LocalDateTime sundayNight{
      .valid = true, .dayIndex = 9'000, .dayOfWeek = 6, .hour = 20, .minute = 59, .second = 30};
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
  std::string failNextWritePath;
  std::string failNextRenameFrom;

  bool read(const std::string& path, uint8_t* destination, size_t capacity, size_t& size) override {
    const auto found = files.find(path);
    if (found == files.end() || found->second.size() > capacity) return false;
    size = found->second.size();
    std::copy(found->second.begin(), found->second.end(), destination);
    return true;
  }

  bool write(const std::string& path, const uint8_t* source, size_t size) override {
    if (path == failNextWritePath) {
      failNextWritePath.clear();
      return false;
    }
    files[path] = std::vector<uint8_t>(source, source + size);
    return true;
  }

  bool exists(const std::string& path) const override { return files.contains(path); }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  bool rename(const std::string& from, const std::string& to) override {
    if (from == failNextRenameFrom) {
      failNextRenameFrom.clear();
      return false;
    }
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

TEST(ReadingStatsStore, KeepsRecoveredBackupWhenReplacementRenameFails) {
  MemoryStatsFiles files;
  ReadingStatsStore store(files);
  ReadingStatsData expected;
  expected.totalReadingSeconds = 1234;
  const auto valid = ReadingStatsCodec::encode(expected);
  files.files["/stats.bin"] = {1, 2, 3};
  files.files["/stats.bin.bak"] = {valid.begin(), valid.end()};

  ReadingStatsData recovered;
  ASSERT_EQ(store.load("/stats.bin", recovered), StatsLoadResult::RecoveredBackup);
  recovered.totalReadingSeconds = 5678;
  files.failNextRenameFrom = "/stats.bin.tmp";
  EXPECT_FALSE(store.save("/stats.bin", recovered));

  ReadingStatsStore reopened(files);
  ReadingStatsData loaded;
  EXPECT_EQ(reopened.load("/stats.bin", loaded), StatsLoadResult::RecoveredBackup);
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

TEST(ReadingStatsStore, ResetAlsoRefusesToOverwriteNewerStatsFile) {
  MemoryStatsFiles files;
  ReadingStatsStore store(files);
  auto newer = ReadingStatsCodec::encode({});
  newer[0] = ReadingStatsCodec::version() + 1;
  files.files["/stats.bin"] = {newer.begin(), newer.end()};

  ReadingStatsData loaded;
  ASSERT_EQ(store.load("/stats.bin", loaded), StatsLoadResult::NewerVersion);
  EXPECT_FALSE(store.reset("/stats.bin"));
  EXPECT_EQ(files.files["/stats.bin"][0], ReadingStatsCodec::version() + 1);
}

TEST(ReaderStatsSession, CommitsSameTruthToBookAndGlobalFiles) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  const LocalDateTime mondayMorning{.valid = true, .dayIndex = 5'000, .dayOfWeek = 0, .hour = 9};
  session.begin("/book-cache", mondayMorning);
  session.pageShown(1'000, mondayMorning);
  EXPECT_TRUE(session.forwardTurn(31'000));
  session.pageShown(32'000, {.valid = true, .dayIndex = 5'000, .dayOfWeek = 0, .hour = 9, .second = 31});
  session.pause(62'000);
  ASSERT_TRUE(session.finish());

  ReadingStatsStore store(files);
  ReadingStatsData book;
  ReadingStatsData global;
  ASSERT_EQ(store.load("/book-cache/reading_stats.bin", book), StatsLoadResult::Ok);
  ASSERT_EQ(store.load(ReaderStatsSession::globalPath(), global), StatsLoadResult::Ok);
  EXPECT_EQ(book.totalSessions, 1u);
  EXPECT_EQ(book.totalReadingSeconds, 60u);
  EXPECT_EQ(book.totalPagesTurned, 1u);
  EXPECT_EQ(book.averageSecondsPerPage, 30u);
  EXPECT_EQ(global.totalSessions, 1u);
  EXPECT_EQ(global.totalReadingSeconds, 60u);
  EXPECT_EQ(global.totalPagesTurned, 1u);
  EXPECT_EQ(global.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Morning)], 60u);
}

TEST(ReaderStatsSession, CompletesBookAndGlobalCountOnlyOnce) {
  MemoryStatsFiles files;
  ReaderStatsSession first(files);
  first.begin("/book-cache", {});
  first.markCompleted(123);
  first.markCompleted(123);
  ASSERT_TRUE(first.finish());

  ReaderStatsSession reopened(files);
  reopened.begin("/book-cache", {});
  reopened.markCompleted(124);
  ASSERT_TRUE(reopened.finish());

  EXPECT_TRUE(reopened.bookStats().completed);
  EXPECT_EQ(reopened.bookStats().finishedDay, 123u);
  EXPECT_EQ(reopened.globalStats().completedBooks, 1u);
}

TEST(ReaderStatsSession, ResetBookDoesNotLetCompletionInflateGlobalCount) {
  MemoryStatsFiles files;
  ReaderStatsSession first(files);
  first.begin("/book-cache", {});
  first.markCompleted(123);
  ASSERT_TRUE(first.finish());

  ReaderStatsSession reset(files);
  reset.begin("/book-cache", {});
  ASSERT_TRUE(reset.resetBook({}));
  reset.markCompleted(124);
  ASSERT_TRUE(reset.finish());

  EXPECT_EQ(reset.globalStats().completedBooks, 1u);
}

TEST(ReaderStatsSession, LiveSnapshotIncludesUnsavedSessionWithoutWriting) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  const LocalDateTime start{.valid = true, .dayIndex = 7000, .dayOfWeek = 2, .hour = 18};
  session.begin("/book-cache", start);
  session.pageShown(1'000, start);
  session.pause(121'000);

  const auto book = session.bookSnapshot();
  EXPECT_EQ(book.totalReadingSeconds, 120u);
  EXPECT_EQ(book.totalSessions, 1u);
  EXPECT_EQ(book.startDay, 7000u);
  EXPECT_FALSE(files.exists("/book-cache/reading_stats.bin"));
}

TEST(ReaderStatsSession, RetriesFailedSaveWithoutApplyingSessionTwice) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  session.begin("/book-cache", {});
  session.pageShown(1'000);
  session.pause(61'000);
  files.failNextWritePath = std::string(ReaderStatsSession::globalPath()) + ".tmp";

  EXPECT_FALSE(session.finish());
  ASSERT_TRUE(session.finish());

  ReadingStatsStore store(files);
  ReadingStatsData book;
  ReadingStatsData global;
  ASSERT_EQ(store.load("/book-cache/reading_stats.bin", book), StatsLoadResult::Ok);
  ASSERT_EQ(store.load(ReaderStatsSession::globalPath(), global), StatsLoadResult::Ok);
  EXPECT_EQ(book.totalSessions, 1u);
  EXPECT_EQ(book.totalReadingSeconds, 60u);
  EXPECT_EQ(global.totalSessions, 1u);
  EXPECT_EQ(global.totalReadingSeconds, 60u);
}

TEST(ReaderStatsSession, ResetBookKeepsGlobalTruthAndStartsFreshBookSession) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  session.begin("/book-cache", {});
  session.pageShown(1'000);
  session.pause(61'000);

  ASSERT_TRUE(session.resetBook({}));
  EXPECT_EQ(session.bookSnapshot().totalReadingSeconds, 0u);
  EXPECT_EQ(session.globalSnapshot().totalReadingSeconds, 60u);

  session.pageShown(100'000);
  session.pause(160'000);
  ASSERT_TRUE(session.finish());
  EXPECT_EQ(session.bookStats().totalReadingSeconds, 60u);
  EXPECT_EQ(session.globalStats().totalReadingSeconds, 120u);
}

TEST(ReaderStatsSession, ResetAllClearsBookAndGlobalTruth) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  session.begin("/book-cache", {});
  session.pageShown(1'000);
  session.pause(61'000);

  ASSERT_TRUE(session.resetAll({}));
  EXPECT_EQ(session.bookSnapshot().totalReadingSeconds, 0u);
  EXPECT_EQ(session.globalSnapshot().totalReadingSeconds, 0u);
}

TEST(ReaderStatsSession, ResetAllInvalidatesStatsForOtherBooks) {
  MemoryStatsFiles files;
  ReaderStatsSession other(files);
  other.begin("/other-cache", {});
  other.pageShown(1'000);
  other.pause(61'000);
  ASSERT_TRUE(other.finish());

  ReaderStatsSession current(files);
  current.begin("/current-cache", {});
  ASSERT_TRUE(current.resetAll({}));

  ReaderStatsSession reopenedOther(files);
  reopenedOther.begin("/other-cache", {});
  EXPECT_EQ(reopenedOther.bookSnapshot().totalReadingSeconds, 0u);
  EXPECT_EQ(reopenedOther.globalSnapshot().totalReadingSeconds, 0u);
}

TEST(ReaderStatsSession, AttributesEachPageDwellToItsRealResumeTime) {
  MemoryStatsFiles files;
  ReaderStatsSession session(files);
  const LocalDateTime mondayMorning{
      .valid = true, .dayIndex = 5'000, .dayOfWeek = 0, .hour = 11, .minute = 59, .second = 30};
  const LocalDateTime tuesdayEvening{.valid = true, .dayIndex = 5'001, .dayOfWeek = 1, .hour = 18};
  session.begin("/book-cache", mondayMorning);
  session.pageShown(1'000, mondayMorning);
  session.pause(31'000);
  session.pageShown(100'000, tuesdayEvening);
  session.pause(130'000);
  ASSERT_TRUE(session.finish());

  const auto& stats = session.bookStats();
  EXPECT_EQ(stats.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Morning)], 30u);
  EXPECT_EQ(stats.timeOfDaySeconds[static_cast<size_t>(TimeOfDay::Evening)], 30u);
  EXPECT_EQ(stats.dayOfWeekSeconds[0], 30u);
  EXPECT_EQ(stats.dayOfWeekSeconds[1], 30u);
}

TEST(ReadingStatsCalendar, RoundTripsLeapDayAndComputesMondayIndex) {
  const CalendarDate leapDay{2024, 2, 29};
  uint32_t dayIndex = 0;
  ASSERT_TRUE(dayIndexFromDate(leapDay, dayIndex));
  CalendarDate decoded;
  ASSERT_TRUE(dateFromDayIndex(dayIndex, decoded));
  EXPECT_EQ(decoded, leapDay);
  EXPECT_EQ(dayOfWeekForDayIndex(dayIndex), 3u);  // Thursday, Monday = 0
}

TEST(ReadingStatsCalendar, RejectsInvalidDates) {
  uint32_t ignored = 0;
  EXPECT_FALSE(dayIndexFromDate({2023, 2, 29}, ignored));
  EXPECT_FALSE(dayIndexFromDate({1999, 12, 31}, ignored));
  EXPECT_FALSE(dayIndexFromDate({2100, 1, 1}, ignored));
}

TEST(ReadingStatsCalendar, AppliesQuarterHourOffsetAcrossMidnight) {
  LocalDateTime local;
  ASSERT_TRUE(makeLocalDateTime({2026, 7, 11}, 23, 50, 0, 4, local));
  CalendarDate date;
  ASSERT_TRUE(dateFromDayIndex(local.dayIndex, date));
  EXPECT_EQ(date, (CalendarDate{2026, 7, 12}));
  EXPECT_EQ(local.hour, 0u);
  EXPECT_EQ(local.minute, 50u);
  EXPECT_EQ(local.dayOfWeek, dayOfWeekForDayIndex(local.dayIndex));
}

TEST(ReadingStatsPresentation, FormatsDurationAndRateForSmallScreen) {
  EXPECT_EQ(formatDuration(0), "0 min");
  EXPECT_EQ(formatDuration(45), "< 1 min");
  EXPECT_EQ(formatDuration(45 * 60), "45 min");
  EXPECT_EQ(formatDuration(2 * 3600 + 30 * 60), "2h 30m");
  EXPECT_FLOAT_EQ(pagesPerMinute(90, 45 * 60), 2.0f);
}

TEST(ReadingStatsPresentation, EstimatesTimeLeftFromProgress) {
  EXPECT_EQ(estimateTimeLeft(3600, 25), 3u * 3600u);
  EXPECT_EQ(estimateTimeLeft(3600, 0), 0u);
  EXPECT_EQ(estimateTimeLeft(3600, 100), 0u);
}

TEST(ReadingStatsPresentation, EstimatesFinishDayFromObservedDailyPace) {
  EXPECT_EQ(estimateFinishDay(110, 100, 11 * 3600, 3 * 3600), 113u);
  EXPECT_EQ(estimateFinishDay(110, 0, 11 * 3600, 3 * 3600), 0u);
  EXPECT_EQ(estimateFinishDay(110, 100, 0, 3 * 3600), 0u);
}

TEST(ReadingStatsPresentation, CentersTextInsideEachMetricCell) {
  EXPECT_EQ(centeredTextX(0, 160, 40), 60);
  EXPECT_EQ(centeredTextX(160, 160, 40), 220);
  EXPECT_EQ(centeredTextX(320, 160, 40), 380);
}

TEST(ReadingStatsPresentation, ReservesLabelWidthBeforeChartBars) {
  EXPECT_EQ(chartLabelColumnWidth(480, 86), 94);
  EXPECT_EQ(chartLabelColumnWidth(100, 86), 76);
}

TEST(ReadingStatsPresentation, InsetsStatsContentEquallyOnBothSides) {
  const auto layout = insetHorizontal(0, 480, 10);
  EXPECT_EQ(layout.x, 10);
  EXPECT_EQ(layout.width, 460);

  const auto narrow = insetHorizontal(4, 12, 10);
  EXPECT_EQ(narrow.x, 10);
  EXPECT_EQ(narrow.width, 0);
}
