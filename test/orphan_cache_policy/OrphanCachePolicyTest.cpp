// Host tests for the orphan-cache decision logic (src/util/BookCacheUtils.h),
// the safety-critical rule behind the on-device "Clean Up Storage" action. The
// filesystem walk needs a real SD card and is device-verified, but the DECISION
// — which /.crosspoint child directories are safe to delete — is pure and locked
// here. The invariant that matters: never flag a present book's cache, and never
// flag a non-cache entry (backups, trash, settings), no matter what.

#include <gtest/gtest.h>

#include <set>
#include <string>

// Header-only pure helpers; no device dependencies pulled in.
#include "BookCacheUtils.h"

namespace {

std::set<std::string> liveSet() {
  // Cache dirs of books still on the card.
  return {"epub_2321488728", "epub_1772908946", "txt_2372652491", "xtc_555"};
}

TEST(OrphanCachePolicy, RecognisesBookCacheDirNames) {
  EXPECT_TRUE(isBookCacheDirectoryName("epub_123"));
  EXPECT_TRUE(isBookCacheDirectoryName("txt_9"));
  EXPECT_TRUE(isBookCacheDirectoryName("xtc_0"));
  EXPECT_FALSE(isBookCacheDirectoryName("backups"));
  EXPECT_FALSE(isBookCacheDirectoryName("trash"));
  EXPECT_FALSE(isBookCacheDirectoryName("settings.json"));
  EXPECT_FALSE(isBookCacheDirectoryName("recent_v2.bin"));
  EXPECT_FALSE(isBookCacheDirectoryName(""));
  EXPECT_FALSE(isBookCacheDirectoryName(nullptr));
  // Prefix must be exact; a lookalike is not a cache dir.
  EXPECT_FALSE(isBookCacheDirectoryName("epubfoo"));
}

TEST(OrphanCachePolicy, PresentBookCacheIsNeverOrphan) {
  const auto live = liveSet();
  for (const auto& present : live) {
    EXPECT_FALSE(isOrphanCacheDir(present, live)) << present << " is live and must be kept";
  }
}

TEST(OrphanCachePolicy, MissingBookCacheIsOrphan) {
  const auto live = liveSet();
  EXPECT_TRUE(isOrphanCacheDir("epub_999999", live));
  EXPECT_TRUE(isOrphanCacheDir("txt_1", live));
  EXPECT_TRUE(isOrphanCacheDir("xtc_42", live));
}

TEST(OrphanCachePolicy, NonCacheEntriesAreNeverOrphan) {
  const auto live = liveSet();
  // These live directly under /.crosspoint and must survive the sweep even
  // though they are absent from the live-book set.
  for (const char* keep : {"backups", "trash", "settings.json", "settings.json.bak", "wifi.bin",
                           "global_reading_stats.bin", "recent_v2.bin", "sleep_index.bin", ".browse_index_a.bin"}) {
    EXPECT_FALSE(isOrphanCacheDir(keep, live)) << keep << " is not a book cache and must never be deleted";
  }
}

TEST(OrphanCachePolicy, EmptyLiveSetOrphansOnlyRealCaches) {
  // Even with no books found, non-cache system entries are still protected.
  const std::set<std::string> empty;
  EXPECT_TRUE(isOrphanCacheDir("epub_1", empty));
  EXPECT_FALSE(isOrphanCacheDir("backups", empty));
  EXPECT_FALSE(isOrphanCacheDir("koreader.json", empty));
}

}  // namespace
