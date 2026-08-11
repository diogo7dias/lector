#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "TaskWatchdog.h"

namespace {

constexpr char CACHE_DIR[] = "/.crosspoint";

constexpr char EPUB_PREFIX[] = "epub_";
constexpr char TXT_PREFIX[] = "txt_";
constexpr char XTC_PREFIX[] = "xtc_";

// Safety caps on the live-book walk. Hitting any of them aborts the walk, and an
// aborted walk deletes nothing, so these trade a refused clean for the risk of
// wiping a real book's progress.
//
// MAX_LIVE_ENTRIES is a memory bound as much as a sanity one: each key is 16
// bytes, so 2000 books cost 32 KB of a 380 KB heap. MAX_DIRS_VISITED bounds the
// walk itself, not the file count — a folder of several thousand wallpapers is
// one directory and stays cheap.
constexpr int MAX_SCAN_DEPTH = 12;
constexpr size_t MAX_DIRS_VISITED = 4000;
constexpr size_t MAX_LIVE_ENTRIES = 2000;

// The walk runs inline on the loop task and can take many seconds on a full card.
// Feed the watchdog periodically or it panics mid-scan.
constexpr uint32_t WDT_YIELD_INTERVAL = 256;

}  // namespace

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

bool parseBookCacheDirName(const char* name, BookCacheKey& key) {
  if (!name) {
    return false;
  }

  const char* digits = nullptr;
  if (strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0) {
    key.prefix = BookCacheKey::EPUB;
    digits = name + std::size(EPUB_PREFIX) - 1;
  } else if (strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0) {
    key.prefix = BookCacheKey::TXT;
    digits = name + std::size(TXT_PREFIX) - 1;
  } else if (strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0) {
    key.prefix = BookCacheKey::XTC;
    digits = name + std::size(XTC_PREFIX) - 1;
  } else {
    return false;
  }

  // The readers build the name as prefix + std::to_string(hash), so anything that
  // is not purely decimal after the prefix was not written by them. Refusing it
  // here keeps such a directory out of the orphan list instead of deleting it.
  if (*digits == '\0') {
    return false;
  }
  char* end = nullptr;
  const unsigned long long parsed = strtoull(digits, &end, 10);
  if (end == digits || *end != '\0') {
    return false;
  }

  key.hash = static_cast<uint64_t>(parsed);
  return true;
}

bool bookCacheKeyForPath(const std::string& path, BookCacheKey& key) {
  if (FsHelpers::hasEpubExtension(path)) {
    key.prefix = BookCacheKey::EPUB;
  } else if (FsHelpers::hasXtcExtension(path)) {
    key.prefix = BookCacheKey::XTC;
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    // .md has no reader of its own and is opened as text, so it caches under txt_.
    key.prefix = BookCacheKey::TXT;
  } else {
    return false;
  }

  key.hash = static_cast<uint64_t>(std::hash<std::string>{}(path));
  return true;
}

std::string bookCacheDirForPath(const std::string& path) {
  BookCacheKey key;
  if (!bookCacheKeyForPath(path, key)) return {};
  const char* prefix = EPUB_PREFIX;
  if (key.prefix == BookCacheKey::TXT) prefix = TXT_PREFIX;
  if (key.prefix == BookCacheKey::XTC) prefix = XTC_PREFIX;
  return std::string(CACHE_DIR) + "/" + prefix + std::to_string(key.hash);
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, CACHE_DIR).clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, CACHE_DIR).clearCache();
  } else if (FsHelpers::hasTxtExtension(path)) {
    Txt(path, CACHE_DIR).clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}

bool collectLiveBookCacheKeys(std::vector<BookCacheKey>& live) {
  live.clear();

  // Iterative depth-first walk with an explicit stack: the ESP32-C3 task stack is
  // far too small to recurse a deep directory tree.
  std::vector<std::pair<std::string, int>> pending;
  pending.emplace_back("/", 0);

  size_t dirsVisited = 0;
  uint32_t entriesSeen = 0;

  while (!pending.empty()) {
    const std::string dirPath = pending.back().first;
    const int depth = pending.back().second;
    pending.pop_back();

    if (++dirsVisited > MAX_DIRS_VISITED) {
      LOG_ERR("BookCache", "live scan aborted: more than %u directories", static_cast<unsigned>(MAX_DIRS_VISITED));
      return false;
    }

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      // A directory we cannot read might hold books, and a book we fail to see
      // looks exactly like an orphan, so refuse the whole clean rather than guess.
      LOG_ERR("BookCache", "live scan aborted: cannot read %s", dirPath.c_str());
      return false;
    }

    char name[256];  // FAT long-file-name maximum (255 chars + terminator)
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if ((++entriesSeen % WDT_YIELD_INTERVAL) == 0) {
        resetTaskWatchdogIfSubscribed();
      }

      const bool isDir = entry.isDirectory();
      entry.getName(name, sizeof(name));
      entry.close();

      // Hidden entries hold no user books, and descending into the cache itself
      // would let cache directories masquerade as live content.
      if (name[0] == '.') {
        continue;
      }

      std::string childPath = dirPath == "/" ? "/" + std::string(name) : dirPath + "/" + name;
      if (childPath == CACHE_DIR) {
        continue;
      }

      if (isDir) {
        if (depth + 1 <= MAX_SCAN_DEPTH) {
          pending.emplace_back(std::move(childPath), depth + 1);
        }
        continue;
      }

      BookCacheKey key;
      if (!bookCacheKeyForPath(childPath, key)) {
        continue;
      }
      if (live.size() >= MAX_LIVE_ENTRIES) {
        LOG_ERR("BookCache", "live scan aborted: more than %u books", static_cast<unsigned>(MAX_LIVE_ENTRIES));
        dir.close();
        return false;
      }
      live.push_back(key);
    }
    dir.close();
  }

  std::sort(live.begin(), live.end());
  LOG_INF("BookCache", "live scan: %u books across %u directories", static_cast<unsigned>(live.size()),
          static_cast<unsigned>(dirsVisited));
  return true;
}

bool cleanOrphanBookCaches(int& removedCount, int& keptCount, int& failedCount) {
  removedCount = 0;
  keptCount = 0;
  failedCount = 0;

  std::vector<BookCacheKey> live;
  if (!collectLiveBookCacheKeys(live)) {
    return false;
  }

  auto root = Storage.open(CACHE_DIR);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    LOG_ERR("BookCache", "cannot open %s", CACHE_DIR);
    return false;
  }

  char name[256];
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    const bool isDir = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();  // must be closed before the directory can be removed

    BookCacheKey key;
    if (!isDir || !parseBookCacheDirName(name, key)) {
      continue;  // settings and anything else under /.crosspoint is never touched
    }

    if (std::binary_search(live.begin(), live.end(), key)) {
      keptCount++;
      continue;
    }

    const std::string fullPath = std::string(CACHE_DIR) + "/" + name;
    LOG_INF("BookCache", "removing orphan cache: %s", fullPath.c_str());
    // A book cache holds one file per laid-out section, so a single removeDir can
    // run for seconds; the SD layer does not feed the watchdog itself.
    resetTaskWatchdogIfSubscribed();
    if (Storage.removeDir(fullPath.c_str())) {
      removedCount++;
    } else {
      LOG_ERR("BookCache", "failed to remove: %s", fullPath.c_str());
      failedCount++;
    }
    resetTaskWatchdogIfSubscribed();
  }
  root.close();

  LOG_INF("BookCache", "clean: %d removed, %d kept, %d failed", removedCount, keptCount, failedCount);
  return true;
}
