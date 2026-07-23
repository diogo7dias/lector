#pragma once
#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  int progressPercent = -1;  // last-read progress 0-100; -1 = unknown (never opened) -> no badge

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore;
namespace JsonSettingsIO {
bool loadRecentBooks(RecentBooksStore& store, const char* json);
}  // namespace JsonSettingsIO

class RecentBooksStore {
  // Static instance
  static RecentBooksStore instance;

  std::vector<RecentBook> recentBooks;
  bool loaded = false;

  // Lazy first-use load: main.cpp no longer reads this store at boot, so every
  // public entry point funnels through here before touching recentBooks.
  void ensureLoaded() const {
    if (!loaded) const_cast<RecentBooksStore*>(this)->loadFromFile();
  }

  friend bool JsonSettingsIO::loadRecentBooks(RecentBooksStore&, const char*);

 public:
  ~RecentBooksStore() = default;

  // Get singleton instance
  static RecentBooksStore& getInstance() { return instance; }

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath);

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath);

  // Record last-read progress (0-100) for the entry with this path and persist it.
  // No-op if no entry matches. Fed by the reader on exit; shown as a [NN%] badge
  // on the Home list.
  void setProgress(const std::string& path, int percent);

  // Remove the entry whose path matches (used when a book is removed from recents or finished/read).
  // Returns true if an entry was found and removed (no-op + false otherwise).
  // Persistence is best-effort: a failed save is logged, not reflected in the return.
  bool removeByPath(const std::string& path);

  // Repoint an entry's path (and coverBmpPath, if it lived under the old cache dir) after the
  // backing file and cache dir were moved on disk. No-op if no entry matches oldPath.
  // Persists on success. Keeps the entry's list position (does not reorder).
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // Move an opened book file into the flat /recents/ folder (feature gated by
  // SETTINGS.moveOpenedToRecents). Reading progress is preserved by relocating
  // the book's hash-keyed cache dir with it, and the _QUOTES.txt sidecar is
  // carried along. Repoints this store's entry and APP_STATE.openEpubPath when
  // they matched. Returns the new path, or the original path unchanged when the
  // move is disabled, unnecessary (already under /recents/, or a non-book type),
  // or fails — on failure the file and its progress are always left intact.
  // Must run before the reader opens the book (no open handle on the file/cache).
  std::string relocateOpenedBookToRecents(const std::string& bookPath);

  // The reader's hash-keyed cache dir for a book path, matching Epub / Txt / Xtc
  // exactly (e.g. "/.crosspoint/epub_<hash>"), or "" for a non-book extension.
  // Same formula the readers use, so callers can locate a book's cached sidecars
  // (progress.bin, reader_override.bin) without opening the book.
  static std::string bookCacheDir(const std::string& bookPath);

  // True if the book's backing file is no longer present on the SD card.
  static bool isMissing(const RecentBook& book);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed. Does not persist — caller decides.
  bool pruneMissing();

  // Get the list of recent books (most recent first)
  const std::vector<RecentBook>& getBooks() const {
    ensureLoaded();
    return recentBooks;
  }

  // Get the count of recent books
  int getCount() const {
    ensureLoaded();
    return static_cast<int>(recentBooks.size());
  }

  bool saveToFile() const;

  bool loadFromFile();
  RecentBook getDataFromBook(std::string path) const;

 private:
  bool loadFromBinaryFile();
  // Fill progressPercent == -1 entries from the per-book progress.bin sidecar's
  // trailing percent byte (survives a lost recent.json). In-RAM only, no save.
  void restoreMissingProgress();
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
