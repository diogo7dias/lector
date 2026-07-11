#pragma once

#include <cstdint>
#include <string>

#include "ReadingStats.h"
#include "ReadingStatsStore.h"

namespace reading_stats {

class ReaderStatsSession {
 public:
  explicit ReaderStatsSession(StatsFiles& files, TrackerConfig config = {});

  static const char* globalPath() { return "/.crosspoint/global_reading_stats.bin"; }

  void configure(TrackerConfig config) { config_ = config; }
  void begin(const std::string& bookCachePath, LocalDateTime localStart);
  void pageShown(uint32_t nowMs);
  bool forwardTurn(uint32_t nowMs);
  void pause(uint32_t nowMs);
  bool finish();
  void markCompleted(uint32_t dayIndex = 0);
  bool resetBook(LocalDateTime newStart);
  bool resetAll(LocalDateTime newStart);

  const ReadingStatsData& bookStats() const { return bookStats_; }
  const ReadingStatsData& globalStats() const { return globalStats_; }
  ReadingStatsData bookSnapshot() const;
  ReadingStatsData globalSnapshot() const;
  const SessionResult& currentSession() const { return tracker_.session(); }

 private:
  void restartTracker(LocalDateTime newStart);

  TrackerConfig config_;
  ReadingStatsStore bookStore_;
  ReadingStatsStore globalStore_;
  ReadingStatsTracker tracker_;
  ReadingStatsData bookStats_;
  ReadingStatsData globalStats_;
  std::string bookStatsPath_;
  LocalDateTime localStart_;
  bool begun_ = false;
  bool finished_ = false;
  bool sessionApplied_ = false;
  bool saved_ = false;
};

}  // namespace reading_stats
