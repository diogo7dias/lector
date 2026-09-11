#include "OpenReadingStats.h"

#include <Arduino.h>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "activities/reader/BookStatsActivity.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsPresentation.h"
#include "reading_stats/ReadingStatsStore.h"
#include "reading_stats/SdStatsFiles.h"
#include "util/BookCacheUtils.h"

bool launchRecentBookStats(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  const auto& books = RECENT_BOOKS.getBooks();
  const RecentBook* book = nullptr;
  for (const auto& candidate : books) {
    if (RecentBooksStore::isMissing(candidate)) continue;
    book = &candidate;
    break;
  }
  if (book == nullptr) return false;
  const std::string cacheDir = bookCacheDirForPath(book->path);
  if (cacheDir.empty()) return false;

  reading_stats::SdStatsFiles files;
  reading_stats::ReadingStatsStore store(files);
  reading_stats::ReadingStatsData bookStats;
  reading_stats::ReadingStatsData globalStats;
  store.load(cacheDir + "/reading_stats.bin", bookStats);
  store.load(reading_stats::ReaderStatsSession::globalPath(), globalStats);

  const uint8_t progress = book->progressPercent > 0 ? static_cast<uint8_t>(book->progressPercent) : 0;

  host.startActivityForResult(std::make_unique<BookStatsActivity>(
                                  renderer, mappedInput, book->title, bookStats, globalStats, progress,
                                  reading_stats::estimateTimeLeft(bookStats.totalReadingSeconds, progress),
                                  [cacheDir](const bool resetAll, reading_stats::ReadingStatsData& outBook,
                                             reading_stats::ReadingStatsData& outGlobal) {
                                    reading_stats::SdStatsFiles resetFiles;
                                    reading_stats::ReadingStatsStore resetStore(resetFiles);
                                    outBook = reading_stats::ReadingStatsData{};
                                    if (!resetStore.reset(cacheDir + "/reading_stats.bin", outBook)) return false;
                                    if (resetAll) {
                                      outGlobal = reading_stats::ReadingStatsData{};
                                      if (!resetStore.reset(reading_stats::ReaderStatsSession::globalPath(), outGlobal))
                                        return false;
                                    } else {
                                      resetStore.load(reading_stats::ReaderStatsSession::globalPath(), outGlobal);
                                    }
                                    return true;
                                  }),
                              [&host](const ActivityResult&) { host.requestUpdate(); });
  return true;
}

void launchLiveReadingStats(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                            reading_stats::ReaderStatsSession& session, const bool trackingActive,
                            const std::string& title, const uint8_t progressPercent) {
  if (trackingActive) session.pause(millis());

  const reading_stats::ReadingStatsData book = session.bookSnapshot();
  const reading_stats::ReadingStatsData global = session.globalSnapshot();
  const uint8_t progress = progressPercent > 100 ? 100 : progressPercent;

  host.startActivityForResult(std::make_unique<BookStatsActivity>(
                                  renderer, mappedInput, title, book, global, progress,
                                  reading_stats::estimateTimeLeft(book.totalReadingSeconds, progress),
                                  [&session](const bool resetAll, reading_stats::ReadingStatsData& nextBook,
                                             reading_stats::ReadingStatsData& nextGlobal) {
                                    const auto now = reading_stats::currentLocalDateTime();
                                    const bool reset = resetAll ? session.resetAll(now) : session.resetBook(now);
                                    if (reset) {
                                      nextBook = session.bookSnapshot();
                                      nextGlobal = session.globalSnapshot();
                                    }
                                    return reset;
                                  }),
                              [&host](const ActivityResult&) { host.requestUpdate(); });
}
