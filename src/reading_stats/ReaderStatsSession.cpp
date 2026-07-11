#include "ReaderStatsSession.h"

#include <limits>

namespace reading_stats {

ReaderStatsSession::ReaderStatsSession(StatsFiles& files, const TrackerConfig config)
    : config_(config), bookStore_(files), globalStore_(files), tracker_(config) {}

void ReaderStatsSession::begin(const std::string& bookCachePath, const LocalDateTime localStart) {
  bookStatsPath_ = bookCachePath + "/reading_stats.bin";
  localStart_ = localStart;
  bookStats_ = {};
  globalStats_ = {};
  bookStore_.load(bookStatsPath_, bookStats_);
  globalStore_.load(globalPath(), globalStats_);
  tracker_ = ReadingStatsTracker(config_);
  begun_ = true;
  finished_ = false;
  sessionApplied_ = false;
  saved_ = false;
}

void ReaderStatsSession::pageShown(const uint32_t nowMs) {
  if (begun_ && !finished_) tracker_.startPage(nowMs);
}

bool ReaderStatsSession::forwardTurn(const uint32_t nowMs) {
  if (!begun_ || finished_) return false;
  const uint32_t before = tracker_.session().readingSeconds;
  const bool accepted = tracker_.forwardTurn(nowMs);
  if (accepted) bookStats_.recordForwardPage(tracker_.session().readingSeconds - before);
  return accepted;
}

void ReaderStatsSession::pause(const uint32_t nowMs) {
  if (begun_ && !finished_) tracker_.pause(nowMs);
}

bool ReaderStatsSession::finish() {
  if (!begun_) return false;
  if (saved_) return true;
  if (!sessionApplied_) {
    const SessionResult result = tracker_.finish();
    bookStats_.apply(result);
    globalStats_.apply(result);
    if (localStart_.valid && result.readingSeconds > 0) {
      bookStats_.recordReadingSpan(localStart_, result.readingSeconds);
      globalStats_.recordReadingSpan(localStart_, result.readingSeconds);
      if (bookStats_.startDay == 0 && result.readingSeconds >= 120) bookStats_.startDay = localStart_.dayIndex;
    }
    sessionApplied_ = true;
    finished_ = true;
  }
  saved_ = bookStore_.save(bookStatsPath_, bookStats_) && globalStore_.save(globalPath(), globalStats_);
  return saved_;
}

void ReaderStatsSession::markCompleted(const uint32_t dayIndex) {
  if (!begun_ || bookStats_.completed) return;
  bookStats_.completed = true;
  bookStats_.finishedDay = dayIndex;
  if (globalStats_.completedBooks != std::numeric_limits<uint32_t>::max()) ++globalStats_.completedBooks;
}

bool ReaderStatsSession::resetBook(const LocalDateTime newStart) {
  if (!finish() || !bookStore_.reset(bookStatsPath_)) return false;
  bookStats_ = {};
  restartTracker(newStart);
  return true;
}

bool ReaderStatsSession::resetAll(const LocalDateTime newStart) {
  if (!finish()) return false;
  if (!globalStore_.reset(globalPath()) || !bookStore_.reset(bookStatsPath_)) return false;
  bookStats_ = {};
  globalStats_ = {};
  restartTracker(newStart);
  return true;
}

void ReaderStatsSession::restartTracker(const LocalDateTime newStart) {
  tracker_ = ReadingStatsTracker(config_);
  localStart_ = newStart;
  finished_ = false;
  sessionApplied_ = false;
  saved_ = false;
}

ReadingStatsData ReaderStatsSession::bookSnapshot() const {
  ReadingStatsData snapshot = bookStats_;
  if (sessionApplied_) return snapshot;
  SessionResult live = tracker_.session();
  live.countsAsSession = live.readingSeconds >= config_.minimumSessionSeconds;
  snapshot.apply(live);
  if (localStart_.valid && tracker_.session().readingSeconds > 0) {
    snapshot.recordReadingSpan(localStart_, tracker_.session().readingSeconds);
    if (snapshot.startDay == 0 && tracker_.session().readingSeconds >= 120) snapshot.startDay = localStart_.dayIndex;
  }
  return snapshot;
}

ReadingStatsData ReaderStatsSession::globalSnapshot() const {
  ReadingStatsData snapshot = globalStats_;
  if (sessionApplied_) return snapshot;
  SessionResult live = tracker_.session();
  live.countsAsSession = live.readingSeconds >= config_.minimumSessionSeconds;
  snapshot.apply(live);
  if (localStart_.valid && tracker_.session().readingSeconds > 0) {
    snapshot.recordReadingSpan(localStart_, tracker_.session().readingSeconds);
  }
  return snapshot;
}

}  // namespace reading_stats
