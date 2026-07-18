#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <functional>
#include <utility>
#include <vector>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_task_wdt.h>
#endif

namespace {
// Safety caps for the recursive book scan. Exceeding any of them aborts the scan
// (collectLiveBookCacheDirs returns false) so the cleaner never deletes on an
// incomplete picture. Generous vs a real reader library, small vs runaway.
constexpr int MAX_SCAN_DEPTH = 12;
constexpr size_t MAX_DIRS_VISITED = 4000;
constexpr size_t MAX_LIVE_ENTRIES = 8000;
constexpr char CACHE_DIR[] = "/.crosspoint";

void wdtYield(size_t& counter) {
#if defined(ARDUINO_ARCH_ESP32)
  if ((++counter % 256) == 0) esp_task_wdt_reset();
#else
  (void)counter;
#endif
}
}  // namespace

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(path)) {
    Txt(path, "/.crosspoint").clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}

std::string bookCacheDirName(const std::string& path) {
  // Mirror the readers' cache keys exactly: "<type>_" + std::to_string(
  // std::hash<std::string>{}(path)). Epub.cpp / Txt.cpp / Xtc.h all hash the
  // opened file path with the same std::hash, so recomputing it here yields the
  // identical directory name the reader created. .md opens through Txt, so it
  // shares the txt_ prefix.
  const char* prefix = nullptr;
  if (FsHelpers::hasEpubExtension(path)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    prefix = "txt_";
  } else {
    return "";
  }
  return std::string(prefix) + std::to_string(std::hash<std::string>{}(path));
}

bool collectLiveBookCacheDirs(std::set<std::string>& live) {
  live.clear();
  // Iterative DFS with an explicit stack of (path, depth); the ESP32-C3 stack is
  // too small for deep recursion. Root path is "" so children read as "/name".
  std::vector<std::pair<std::string, int>> stack;
  stack.emplace_back("", 0);
  size_t dirsVisited = 0;
  size_t wdt = 0;
  char nameBuffer[256];

  while (!stack.empty()) {
    const std::string dirPath = stack.back().first;
    const int depth = stack.back().second;
    stack.pop_back();

    if (++dirsVisited > MAX_DIRS_VISITED) {
      LOG_ERR("CLEAN", "Book scan exceeded %u directories; aborting", static_cast<unsigned>(MAX_DIRS_VISITED));
      return false;
    }

    const std::string openPath = dirPath.empty() ? "/" : dirPath;
    auto dir = Storage.open(openPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      LOG_ERR("CLEAN", "Could not read directory %s; aborting (delete nothing)", openPath.c_str());
      return false;
    }

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      wdtYield(wdt);
      entry.getName(nameBuffer, sizeof(nameBuffer));
      const std::string name(nameBuffer);
      const bool isDir = entry.isDirectory();
      entry.close();

      // Skip empties and hidden entries (the browser hides dot-names too, so a
      // book buried in one is not openable and needs no cache-liveness record).
      if (name.empty() || name[0] == '.') continue;

      const std::string childPath = dirPath + "/" + name;

      if (isDir) {
        // Never descend into the cache tree itself.
        if (childPath == CACHE_DIR) continue;
        if (depth + 1 <= MAX_SCAN_DEPTH) {
          stack.emplace_back(childPath, depth + 1);
        }
        continue;
      }

      const std::string cacheName = bookCacheDirName(childPath);
      if (cacheName.empty()) continue;
      live.insert(cacheName);
      if (live.size() > MAX_LIVE_ENTRIES) {
        LOG_ERR("CLEAN", "Book scan exceeded %u live entries; aborting", static_cast<unsigned>(MAX_LIVE_ENTRIES));
        dir.close();
        return false;
      }
    }
    dir.close();
  }

  LOG_DBG("CLEAN", "Live book caches on card: %u", static_cast<unsigned>(live.size()));
  return true;
}

bool cleanOrphanBookCaches(int& removed, int& kept, int& failed) {
  removed = 0;
  kept = 0;
  failed = 0;

  std::set<std::string> live;
  if (!collectLiveBookCacheDirs(live)) {
    LOG_ERR("CLEAN", "Live-book scan failed; aborting, nothing deleted");
    return false;
  }

  auto root = Storage.open(CACHE_DIR);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    LOG_ERR("CLEAN", "Could not open %s", CACHE_DIR);
    return false;
  }

  char name[256];
  root.rewindDirectory();
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    const std::string dirName(name);
    const bool isDir = entry.isDirectory();
    // Close before any remove: the entry must not hold the directory open.
    entry.close();

    if (!isDir || !isBookCacheDirectoryName(dirName.c_str())) continue;

    if (live.find(dirName) != live.end()) {
      ++kept;
      continue;
    }

    const std::string fullPath = std::string(CACHE_DIR) + "/" + dirName;
    if (Storage.removeDir(fullPath.c_str())) {
      ++removed;
      LOG_DBG("CLEAN", "Removed orphan cache: %s", fullPath.c_str());
    } else {
      ++failed;
      LOG_ERR("CLEAN", "Failed to remove orphan: %s", fullPath.c_str());
    }
  }
  root.close();

  LOG_INF("CLEAN", "Orphan cleanup: %d removed, %d kept, %d failed", removed, kept, failed);
  return true;
}
