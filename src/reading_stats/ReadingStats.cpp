#include "ReadingStats.h"

#include <algorithm>
#include <limits>

namespace reading_stats {
namespace {

bool leapYear(const uint16_t year) { return (year % 4u == 0 && year % 100u != 0) || year % 400u == 0; }

uint8_t monthDays(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && leapYear(year) ? 29 : days[month - 1];
}

uint32_t saturatedAdd(const uint32_t left, const uint32_t right) {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  return maximum - left < right ? maximum : left + right;
}

bool historyBit(const std::array<uint8_t, kReadingHistoryBytes>& bits, const size_t index) {
  return index < kReadingHistoryDays && (bits[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
}

void setHistoryBit(std::array<uint8_t, kReadingHistoryBytes>& bits, const size_t index) {
  if (index < kReadingHistoryDays) bits[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
}

void shiftHistoryOlder(std::array<uint8_t, kReadingHistoryBytes>& bits, const uint32_t days) {
  if (days >= kReadingHistoryDays) {
    bits.fill(0);
    return;
  }
  std::array<uint8_t, kReadingHistoryBytes> shifted{};
  for (size_t i = 0; i + days < kReadingHistoryDays; ++i) {
    if (historyBit(bits, i)) setHistoryBit(shifted, i + days);
  }
  bits = shifted;
}

TimeOfDay bucketForHour(const uint8_t hour) {
  if (hour >= 5 && hour < 12) return TimeOfDay::Morning;
  if (hour >= 12 && hour < 17) return TimeOfDay::Afternoon;
  if (hour >= 17 && hour < 21) return TimeOfDay::Evening;
  return TimeOfDay::Night;
}

uint32_t secondsUntilBoundary(const LocalDateTime& time) {
  const uint32_t secondOfDay =
      static_cast<uint32_t>(time.hour) * 3600u + static_cast<uint32_t>(time.minute) * 60u + time.second;
  uint32_t boundary = 24u * 3600u;
  if (time.hour < 5)
    boundary = 5u * 3600u;
  else if (time.hour < 12)
    boundary = 12u * 3600u;
  else if (time.hour < 17)
    boundary = 17u * 3600u;
  else if (time.hour < 21)
    boundary = 21u * 3600u;
  return std::max<uint32_t>(1, boundary - secondOfDay);
}

void advance(LocalDateTime& time, const uint32_t seconds) {
  uint32_t secondOfDay =
      static_cast<uint32_t>(time.hour) * 3600u + static_cast<uint32_t>(time.minute) * 60u + time.second;
  secondOfDay += seconds;
  const uint32_t days = secondOfDay / (24u * 3600u);
  secondOfDay %= 24u * 3600u;
  time.dayIndex += days;
  time.dayOfWeek = static_cast<uint8_t>((time.dayOfWeek + days) % 7u);
  time.hour = static_cast<uint8_t>(secondOfDay / 3600u);
  secondOfDay %= 3600u;
  time.minute = static_cast<uint8_t>(secondOfDay / 60u);
  time.second = static_cast<uint8_t>(secondOfDay % 60u);
}

void write16(uint8_t* bytes, const size_t offset, const uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* bytes, const size_t offset, const uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read16(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1]) << 8;
}

uint32_t read32(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | static_cast<uint32_t>(bytes[offset + 1]) << 8 |
         static_cast<uint32_t>(bytes[offset + 2]) << 16 | static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

}  // namespace

bool dayIndexFromDate(const CalendarDate date, uint32_t& dayIndex) {
  if (date.year < 2000 || date.year > 2099 || date.day < 1 || date.day > monthDays(date.year, date.month)) return false;
  uint32_t result = 0;
  for (uint16_t year = 2000; year < date.year; ++year) result += leapYear(year) ? 366u : 365u;
  for (uint8_t month = 1; month < date.month; ++month) result += monthDays(date.year, month);
  dayIndex = result + date.day - 1u;
  return true;
}

bool dateFromDayIndex(uint32_t dayIndex, CalendarDate& date) {
  uint16_t year = 2000;
  while (year <= 2099) {
    const uint32_t days = leapYear(year) ? 366u : 365u;
    if (dayIndex < days) break;
    dayIndex -= days;
    ++year;
  }
  if (year > 2099) return false;
  uint8_t month = 1;
  while (month <= 12 && dayIndex >= monthDays(year, month)) {
    dayIndex -= monthDays(year, month);
    ++month;
  }
  if (month > 12) return false;
  date = {year, month, static_cast<uint8_t>(dayIndex + 1u)};
  return true;
}

uint8_t dayOfWeekForDayIndex(const uint32_t dayIndex) { return static_cast<uint8_t>((5u + dayIndex) % 7u); }

bool makeLocalDateTime(const CalendarDate utcDate, const uint8_t hour, const uint8_t minute, const uint8_t second,
                       const int offsetQuarterHours, LocalDateTime& local) {
  uint32_t dayIndex = 0;
  if (!dayIndexFromDate(utcDate, dayIndex) || hour >= 24 || minute >= 60 || second >= 60 || offsetQuarterHours < -48 ||
      offsetQuarterHours > 56)
    return false;

  int totalMinutes = static_cast<int>(hour) * 60 + minute + offsetQuarterHours * 15;
  while (totalMinutes < 0) {
    if (dayIndex == 0) return false;
    --dayIndex;
    totalMinutes += 24 * 60;
  }
  while (totalMinutes >= 24 * 60) {
    ++dayIndex;
    totalMinutes -= 24 * 60;
  }
  CalendarDate checked;
  if (!dateFromDayIndex(dayIndex, checked)) return false;
  local = {.valid = true,
           .dayIndex = dayIndex,
           .dayOfWeek = dayOfWeekForDayIndex(dayIndex),
           .hour = static_cast<uint8_t>(totalMinutes / 60),
           .minute = static_cast<uint8_t>(totalMinutes % 60),
           .second = second};
  return true;
}

ReadingStatsTracker::ReadingStatsTracker(const TrackerConfig config) : config_(config) {}

void ReadingStatsTracker::startPage(const uint32_t nowMs) {
  if (pageActive_) return;
  pageStartedAtMs_ = nowMs;
  pageActive_ = true;
}

bool ReadingStatsTracker::commitPage(const uint32_t nowMs, const bool countForwardTurn) {
  if (!pageActive_) return false;
  pageActive_ = false;

  const uint32_t elapsedSeconds = (nowMs - pageStartedAtMs_) / 1000;
  if (elapsedSeconds < config_.minimumPageSeconds || elapsedSeconds > config_.idleThresholdSeconds) return false;

  session_.readingSeconds = saturatedAdd(session_.readingSeconds, elapsedSeconds);
  if (countForwardTurn) session_.pagesTurned = saturatedAdd(session_.pagesTurned, 1);
  return true;
}

bool ReadingStatsTracker::forwardTurn(const uint32_t nowMs) { return commitPage(nowMs, true); }

void ReadingStatsTracker::pause(const uint32_t nowMs) { commitPage(nowMs, false); }

SessionResult ReadingStatsTracker::finish() {
  session_.countsAsSession = session_.readingSeconds >= config_.minimumSessionSeconds;
  return session_;
}

void ReadingStatsData::apply(const SessionResult& result) {
  totalReadingSeconds = saturatedAdd(totalReadingSeconds, result.readingSeconds);
  totalPagesTurned = saturatedAdd(totalPagesTurned, result.pagesTurned);
  if (result.countsAsSession) totalSessions = saturatedAdd(totalSessions, 1);
}

void ReadingStatsData::recordForwardPage(const uint32_t seconds) {
  if (seconds == 0) return;
  const uint32_t oldCount = paceSampleCount;
  const uint32_t newCount = std::min<uint32_t>(oldCount + 1u, std::numeric_limits<uint16_t>::max());
  const uint64_t weighted = static_cast<uint64_t>(averageSecondsPerPage) * oldCount + seconds;
  averageSecondsPerPage = static_cast<uint16_t>(
      std::min<uint64_t>((weighted + newCount / 2u) / newCount, std::numeric_limits<uint16_t>::max()));
  paceSampleCount = static_cast<uint16_t>(newCount);
}

void ReadingStatsData::recordReadingSpan(LocalDateTime start, uint32_t seconds) {
  if (!start.valid || start.dayOfWeek >= kDayOfWeekCount || start.hour >= 24 || start.minute >= 60 ||
      start.second >= 60)
    return;
  while (seconds > 0) {
    const uint32_t segment = std::min(seconds, secondsUntilBoundary(start));
    const size_t bucket = static_cast<size_t>(bucketForHour(start.hour));
    timeOfDaySeconds[bucket] = saturatedAdd(timeOfDaySeconds[bucket], segment);
    dayOfWeekSeconds[start.dayOfWeek] = saturatedAdd(dayOfWeekSeconds[start.dayOfWeek], segment);
    markReadingDay(start.dayIndex);
    seconds -= segment;
    advance(start, segment);
  }
}

void ReadingStatsData::markReadingDay(const uint32_t dayIndex) {
  const bool empty = readingHistoryAnchorDay == 0 && !historyBit(readingHistoryBits, 0);
  if (empty) {
    readingHistoryAnchorDay = dayIndex;
    readingHistoryBits.fill(0);
    setHistoryBit(readingHistoryBits, 0);
  } else if (dayIndex > readingHistoryAnchorDay) {
    shiftHistoryOlder(readingHistoryBits, dayIndex - readingHistoryAnchorDay);
    readingHistoryAnchorDay = dayIndex;
    setHistoryBit(readingHistoryBits, 0);
  } else {
    const uint32_t age = readingHistoryAnchorDay - dayIndex;
    if (age < kReadingHistoryDays) setHistoryBit(readingHistoryBits, age);
  }

  uint16_t best = 0;
  uint16_t run = 0;
  for (int i = static_cast<int>(kReadingHistoryDays) - 1; i >= 0; --i) {
    if (historyBit(readingHistoryBits, static_cast<size_t>(i))) {
      best = std::max<uint16_t>(best, ++run);
    } else {
      run = 0;
    }
  }
  longestReadingStreak = std::max(longestReadingStreak, best);
}

uint16_t ReadingStatsData::currentStreak(const uint32_t todayDay) const {
  if (!historyBit(readingHistoryBits, 0) || todayDay > readingHistoryAnchorDay + 1u) return 0;
  uint16_t streak = 0;
  while (streak < kReadingHistoryDays && historyBit(readingHistoryBits, streak)) ++streak;
  return streak;
}

ReadingStatsCodec::Encoded ReadingStatsCodec::encode(const ReadingStatsData& stats) {
  Encoded bytes{};
  bytes[0] = version();
  bytes[1] = stats.completed ? 1 : 0;
  write32(bytes.data(), 2, stats.totalSessions);
  write32(bytes.data(), 6, stats.totalReadingSeconds);
  write32(bytes.data(), 10, stats.totalPagesTurned);
  write32(bytes.data(), 14, stats.completedBooks);
  write16(bytes.data(), 18, stats.averageSecondsPerPage);
  write16(bytes.data(), 20, stats.paceSampleCount);
  write32(bytes.data(), 22, stats.estimatedTimeLeftSeconds);
  write32(bytes.data(), 26, stats.startDay);
  write32(bytes.data(), 30, stats.finishedDay);
  for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i)
    write32(bytes.data(), 34 + i * 4, stats.timeOfDaySeconds[i]);
  for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i)
    write32(bytes.data(), 50 + i * 4, stats.dayOfWeekSeconds[i]);
  write32(bytes.data(), 78, stats.readingHistoryAnchorDay);
  std::copy(stats.readingHistoryBits.begin(), stats.readingHistoryBits.end(), bytes.begin() + 82);
  write16(bytes.data(), 174, stats.longestReadingStreak);
  return bytes;
}

DecodeResult ReadingStatsCodec::decode(const uint8_t* bytes, const size_t size, ReadingStatsData& stats) {
  if (bytes == nullptr || size != encodedSize()) return DecodeResult::Invalid;
  if (bytes[0] > version()) return DecodeResult::NewerVersion;
  if (bytes[0] != version() || (bytes[1] & ~1u) != 0) return DecodeResult::Invalid;

  ReadingStatsData decoded;
  decoded.completed = (bytes[1] & 1u) != 0;
  decoded.totalSessions = read32(bytes, 2);
  decoded.totalReadingSeconds = read32(bytes, 6);
  decoded.totalPagesTurned = read32(bytes, 10);
  decoded.completedBooks = read32(bytes, 14);
  decoded.averageSecondsPerPage = read16(bytes, 18);
  decoded.paceSampleCount = read16(bytes, 20);
  decoded.estimatedTimeLeftSeconds = read32(bytes, 22);
  decoded.startDay = read32(bytes, 26);
  decoded.finishedDay = read32(bytes, 30);
  for (size_t i = 0; i < decoded.timeOfDaySeconds.size(); ++i) decoded.timeOfDaySeconds[i] = read32(bytes, 34 + i * 4);
  for (size_t i = 0; i < decoded.dayOfWeekSeconds.size(); ++i) decoded.dayOfWeekSeconds[i] = read32(bytes, 50 + i * 4);
  decoded.readingHistoryAnchorDay = read32(bytes, 78);
  std::copy(bytes + 82, bytes + 174, decoded.readingHistoryBits.begin());
  decoded.longestReadingStreak = read16(bytes, 174);
  stats = decoded;
  return DecodeResult::Ok;
}

}  // namespace reading_stats
