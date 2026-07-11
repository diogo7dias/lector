#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reading_stats {

inline constexpr size_t kTimeOfDayBucketCount = 4;
inline constexpr size_t kDayOfWeekCount = 7;
inline constexpr size_t kReadingHistoryBytes = 92;
inline constexpr size_t kReadingHistoryDays = 730;

enum class TimeOfDay : uint8_t { Morning = 0, Afternoon, Evening, Night };

struct CalendarDate {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  bool operator==(const CalendarDate&) const = default;
};

struct LocalDateTime;

bool dayIndexFromDate(CalendarDate date, uint32_t& dayIndex);
bool dateFromDayIndex(uint32_t dayIndex, CalendarDate& date);
uint8_t dayOfWeekForDayIndex(uint32_t dayIndex);
bool makeLocalDateTime(CalendarDate utcDate, uint8_t hour, uint8_t minute, uint8_t second, int offsetQuarterHours,
                       LocalDateTime& local);

struct LocalDateTime {
  bool valid = false;
  uint32_t dayIndex = 0;
  uint8_t dayOfWeek = 0;  // Monday = 0
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

struct SessionResult {
  uint32_t readingSeconds = 0;
  uint32_t pagesTurned = 0;
  bool countsAsSession = false;
};

struct TrackerConfig {
  uint32_t idleThresholdSeconds = 300;
  uint32_t minimumPageSeconds = 2;
  uint32_t minimumSessionSeconds = 60;
};

class ReadingStatsTracker {
 public:
  explicit ReadingStatsTracker(TrackerConfig config = {});

  void startPage(uint32_t nowMs);
  bool forwardTurn(uint32_t nowMs);
  void pause(uint32_t nowMs);
  SessionResult finish();
  const SessionResult& session() const { return session_; }

 private:
  bool commitPage(uint32_t nowMs, bool countForwardTurn);

  TrackerConfig config_;
  SessionResult session_;
  uint32_t pageStartedAtMs_ = 0;
  bool pageActive_ = false;
};

struct ReadingStatsData {
  uint32_t totalSessions = 0;
  uint32_t resetEpoch = 0;
  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t completedBooks = 0;
  uint16_t averageSecondsPerPage = 0;
  uint16_t paceSampleCount = 0;
  uint32_t estimatedTimeLeftSeconds = 0;
  uint32_t startDay = 0;
  uint32_t finishedDay = 0;
  std::array<uint32_t, kTimeOfDayBucketCount> timeOfDaySeconds{};
  std::array<uint32_t, kDayOfWeekCount> dayOfWeekSeconds{};
  uint32_t readingHistoryAnchorDay = 0;
  std::array<uint8_t, kReadingHistoryBytes> readingHistoryBits{};
  uint16_t longestReadingStreak = 0;
  bool completed = false;
  bool completionCredited = false;

  void apply(const SessionResult& result);
  void recordForwardPage(uint32_t seconds);
  void recordReadingSpan(LocalDateTime start, uint32_t seconds);
  void markReadingDay(uint32_t dayIndex);
  uint16_t currentStreak(uint32_t todayDay) const;
  bool operator==(const ReadingStatsData&) const = default;
};

enum class DecodeResult : uint8_t { Ok, Invalid, NewerVersion };

class ReadingStatsCodec {
 public:
  static constexpr size_t kEncodedSize = 180;
  static constexpr uint8_t version() { return 2; }
  static constexpr size_t encodedSize() { return kEncodedSize; }

  using Encoded = std::array<uint8_t, kEncodedSize>;

  static Encoded encode(const ReadingStatsData& stats);
  static DecodeResult decode(const uint8_t* bytes, size_t size, ReadingStatsData& stats);
};

}  // namespace reading_stats
