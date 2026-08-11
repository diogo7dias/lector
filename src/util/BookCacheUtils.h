#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// Identity of one book's cache directory, split so it can be held cheaply.
//
// The obvious container for "every live book" is a set of directory-name strings,
// but a card with a few hundred books would then cost hundreds of kilobytes of a
// 380 KB heap. A cache directory name is fully described by its prefix and the
// std::hash of the book path, so 16 bytes hold what a std::string would spend ~90
// on, and a sorted vector replaces the per-node allocation of a std::set.
struct BookCacheKey {
  enum Prefix : uint8_t { EPUB = 0, TXT = 1, XTC = 2 };

  uint8_t prefix = EPUB;
  uint64_t hash = 0;

  bool operator<(const BookCacheKey& other) const {
    return prefix != other.prefix ? prefix < other.prefix : hash < other.hash;
  }
  bool operator==(const BookCacheKey& other) const { return prefix == other.prefix && hash == other.hash; }
};

// Parses a "/.crosspoint" child directory name back into its key. Returns false
// when the name is not a book cache directory at all.
bool parseBookCacheDirName(const char* name, BookCacheKey& key);

// The cache directory key a book at `path` would use, mirroring what the readers
// themselves compute (prefix + std::hash of the full path). Returns false for
// extensions no reader caches.
bool bookCacheKeyForPath(const std::string& path, BookCacheKey& key);

// Walks the card and collects the cache key of every book still present, sorted.
//
// Returns false when the walk could not finish — a read error, or one of the
// safety caps. The caller MUST then delete nothing: a book the walk missed is
// indistinguishable from an orphan, and its cache directory holds the reading
// progress that would be destroyed.
bool collectLiveBookCacheKeys(std::vector<BookCacheKey>& live);

// The cache directory a book at `path` uses, e.g. "/.crosspoint/epub_1234...". Empty for
// extensions no reader caches. Same prefix + hash the readers compute for themselves, so
// callers outside a reader (the home screen reading a book's stats without opening it) do
// not have to reproduce that rule.
std::string bookCacheDirForPath(const std::string& path);

// Removes every /.crosspoint cache directory whose book is no longer on the card.
// Returns false, having deleted nothing, when the live-book walk fails.
bool cleanOrphanBookCaches(int& removedCount, int& keptCount, int& failedCount);
