#pragma once

#include <cstring>
#include <iterator>
#include <set>
#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry. Inline + pure
// (prefix match only) so it stays testable and cheap on the hot cleanup loop.
inline bool isBookCacheDirectoryName(const char* name) {
  if (!name) return false;
  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";
  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

// Returns the cache directory basename ("epub_<hash>" / "txt_<hash>" /
// "xtc_<hash>") a book file at `path` would use, computed with the SAME hash the
// readers use, or "" if the path is not a recognised book type. Pure: hashes the
// path string only, touches no filesystem.
std::string bookCacheDirName(const std::string& path);

// Decides whether a /.crosspoint child directory named `dirName` is an orphan
// (a book cache whose book is no longer present) given the set of `live` cache
// dir basenames. True only for a real book-cache dir absent from `live`; any
// non-cache entry (backups, trash, settings files) is never an orphan.
inline bool isOrphanCacheDir(const std::string& dirName, const std::set<std::string>& live) {
  return isBookCacheDirectoryName(dirName.c_str()) && live.find(dirName) == live.end();
}

// Recursively scans the card for book files and inserts, into `live`, the cache
// directory basename of every book still present. Returns false on any directory
// read failure or if the safety caps are exceeded — the caller MUST then abort
// and delete nothing, because a missed book would look like an orphan.
bool collectLiveBookCacheDirs(std::set<std::string>& live);

// Removes cache directories under /.crosspoint whose book file no longer exists,
// preserving the caches (progress, images, thumbnails) of books still on the
// card. Fills counts. Returns false if the live-book scan failed, in which case
// NOTHING is deleted.
bool cleanOrphanBookCaches(int& removed, int& kept, int& failed);
