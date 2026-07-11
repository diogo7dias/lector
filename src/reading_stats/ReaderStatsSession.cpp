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
  if (bookStats_.resetEpoch != globalStats_.resetEpoch) {
    bookStats_ = {};
    bookStats_.resetEpoch = globalStats_.resetEpoch;
  }
  tracker_ = ReadingStatsTracker(config_);
  begun_ = true;
  finished_ = false;
  sessionApplied_ = false;
  saved_ = false;
  pageActive_ = false;
}

void ReaderStatsSession::pageShown(const uint32_t nowMs, const LocalDateTime localPageStart) {
  if (!begun_ || finished_) return;
  if (!pageActive_) {
    pageLocalStart_ = localPageStart;
    pageActive_ = true;
  }
  tracker_.startPage(nowMs);
}

bool ReaderStatsSession::forwardTurn(const uint32_t nowMs) {
  if (!begun_ || finished_) return false;
  const uint32_t before = tracker_.session().readingSeconds;
  const bool accepted = tracker_.forwardTurn(nowMs);
  pageActive_ = false;
  if (accepted) {
    const uint32_t seconds = tracker_.session().readingSeconds - before;
    bookStats_.recordForwardPage(seconds);
    recordAcceptedSpan(seconds);
  }
  return accepted;
}

void ReaderStatsSession::pause(const uint32_t nowMs) {
  if (!begun_ || finished_) return;
  const uint32_t before = tracker_.session().readingSeconds;
  tracker_.pause(nowMs);
  pageActive_ = false;
  recordAcceptedSpan(tracker_.session().readingSeconds - before);
}

bool ReaderStatsSession::finish() {
  if (!begun_) return false;
  if (saved_) return true;
  if (!sessionApplied_) {
    const SessionResult result = tracker_.finish();
    bookStats_.apply(result);
    globalStats_.apply(result);
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
  if (!bookStats_.completionCredited) {
    if (globalStats_.completedBooks != std::numeric_limits<uint32_t>::max()) ++globalStats_.completedBooks;
    bookStats_.completionCredited = true;
  }
}

bool ReaderStatsSession::resetBook(const LocalDateTime newStart) {
  if (!finish()) return false;
  ReadingStatsData replacement;
  replacement.resetEpoch = globalStats_.resetEpoch;
  replacement.completionCredited = bookStats_.completionCredited || bookStats_.completed;
  if (!bookStore_.reset(bookStatsPath_, replacement)) return false;
  bookStats_ = replacement;
  restartTracker(newStart);
  return true;
}

bool ReaderStatsSession::resetAll(const LocalDateTime newStart) {
  if (!finish()) return false;
  const uint32_t nextEpoch =
      globalStats_.resetEpoch == std::numeric_limits<uint32_t>::max() ? 1u : globalStats_.resetEpoch + 1u;
  ReadingStatsData replacement;
  replacement.resetEpoch = nextEpoch;
  if (!globalStore_.reset(globalPath(), replacement)) return false;
  bookStore_.reset(bookStatsPath_, replacement);
  bookStats_ = replacement;
  globalStats_ = replacement;
  restartTracker(newStart);
  return true;
}

void ReaderStatsSession::restartTracker(const LocalDateTime newStart) {
  tracker_ = ReadingStatsTracker(config_);
  localStart_ = newStart;
  finished_ = false;
  sessionApplied_ = false;
  saved_ = false;
  pageActive_ = false;
}

void ReaderStatsSession::recordAcceptedSpan(const uint32_t seconds) {
  if (seconds == 0 || !pageLocalStart_.valid) return;
  bookStats_.recordReadingSpan(pageLocalStart_, seconds);
  globalStats_.recordReadingSpan(pageLocalStart_, seconds);
  if (bookStats_.startDay == 0 && tracker_.session().readingSeconds >= 120) {
    bookStats_.startDay = localStart_.valid ? localStart_.dayIndex : pageLocalStart_.dayIndex;
  }
}

ReadingStatsData ReaderStatsSession::bookSnapshot() const {
  ReadingStatsData snapshot = bookStats_;
  if (sessionApplied_) return snapshot;
  SessionResult live = tracker_.session();
  live.countsAsSession = live.readingSeconds >= config_.minimumSessionSeconds;
  snapshot.apply(live);
  return snapshot;
}

ReadingStatsData ReaderStatsSession::globalSnapshot() const {
  ReadingStatsData snapshot = globalStats_;
  if (sessionApplied_) return snapshot;
  SessionResult live = tracker_.session();
  live.countsAsSession = live.readingSeconds >= config_.minimumSessionSeconds;
  snapshot.apply(live);
  return snapshot;
}

}  // namespace reading_stats
