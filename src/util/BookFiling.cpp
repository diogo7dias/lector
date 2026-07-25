#include "util/BookFiling.h"

#include <HalStorage.h>
#include <Logging.h>

#include <functional>

#include "CrossPointState.h"
#include "RecentBooksStore.h"

namespace bookfiling {

std::string buildFolderDestination(const std::string& srcPath, const char* folder) {
  if (folder[0] != '\0') Storage.mkdir(folder);  // the root already exists

  // Bounded: a folder already holding 99 books of the same name is a user problem, not
  // a reason to spin here. The last candidate is returned and the rename fails loudly.
  for (int index = 1; index < 100; index++) {
    std::string candidate = destinationCandidate(srcPath, folder, index);
    if (!Storage.exists(candidate.c_str())) return candidate;
  }
  return destinationCandidate(srcPath, folder, 100);
}

std::string moveBookToFolder(const std::string& srcPath, const std::string& dstPath, const std::string& cachePathHint) {
  LOG_INF("FILE", "Filing book: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("FILE", "Failed to move book to '%s'", dstPath.c_str());
    return srcPath;
  }

  // Cache dir is keyed by hash of the book path (see Epub.h / Txt.cpp / Xtc.h), so it
  // must be re-keyed or the book reopens with no layout and no saved place. The caller
  // may pass the open book's own cache path; otherwise it is derived from the name.
  const std::string oldCachePath = cachePathHint.empty() ? cacheDirFor(srcPath) : cachePathHint;
  const std::string newCachePath = cacheDirFor(dstPath);
  if (!oldCachePath.empty() && !newCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("FILE", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  // Keep the book in recents: repoint the entry instead of dropping it. updatePath
  // persists on success.
  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
  return dstPath;
}

std::string unfileFromRecents(const std::string& srcPath, const std::string& cachePathHint) {
  if (!isInFolder(srcPath, RECENTS_FOLDER)) return srcPath;
  return moveBookToFolder(srcPath, buildFolderDestination(srcPath, ROOT_FOLDER), cachePathHint);
}

}  // namespace bookfiling
