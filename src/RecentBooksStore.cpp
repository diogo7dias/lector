#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>
#include <functional>
#include <iterator>

#include "BookRelocation.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 3;
constexpr char RECENT_BOOKS_FILE_BIN[] = "/.crosspoint/recent.bin";
constexpr char RECENT_BOOKS_FILE_JSON[] = "/.crosspoint/recent.json";
constexpr char RECENT_BOOKS_FILE_BAK[] = "/.crosspoint/recent.bin.bak";
constexpr int MAX_RECENT_BOOKS = 10;

// Best-effort [NN%] badge fallback. The badge value lives in recent.json,
// which a firmware/store migration can lose; each book's progress.bin sidecar
// survives (it also holds the precious reading position). New-format sidecars
// carry a trailing percent byte — EPUB at offset 6, TXT/XTC at offset 4;
// 255 or a short (old-format) file means unknown. Cache dirs are derived from
// the same std::hash the readers use, so no book is ever opened here.
int readSidecarPercent(const std::string& bookPath) {
  const char* prefix;
  size_t offset;
  if (FsHelpers::hasEpubExtension(bookPath)) {
    prefix = "/.crosspoint/epub_";
    offset = 6;
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    prefix = "/.crosspoint/xtc_";
    offset = 4;
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    prefix = "/.crosspoint/txt_";
    offset = 4;
  } else {
    return -1;
  }
  const std::string sidecar = prefix + std::to_string(std::hash<std::string>{}(bookPath)) + "/progress.bin";
  HalFile f;
  if (!Storage.openFileForRead("RBS", sidecar, f)) return -1;
  uint8_t buf[8];
  const int got = f.read(buf, sizeof(buf));
  if (got < static_cast<int>(offset) + 1) return -1;
  return buf[offset] <= 100 ? buf[offset] : -1;
}

// The reader's hash-keyed cache dir for a book path, matching Epub.cpp / Txt.cpp
// / Xtc.h exactly, or "" for a non-book extension. Because it is the same
// std::hash the readers use, relocating this dir with the file lets a moved book
// keep its rendered sections and reading position.
std::string bookCacheDir(const std::string& bookPath) {
  const char* prefix;
  if (FsHelpers::hasEpubExtension(bookPath)) {
    prefix = "/.crosspoint/epub_";
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    prefix = "/.crosspoint/xtc_";
  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    prefix = "/.crosspoint/txt_";
  } else {
    return "";
  }
  return prefix + std::to_string(std::hash<std::string>{}(bookPath));
}
}  // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  ensureLoaded();

  // Reopening the current front book with unchanged metadata is the common
  // resume-wake case. The list would come out byte-identical, so skip the
  // whole pass — pruneMissing() alone costs one FAT existence lookup per
  // recent entry, plus the file rewrite.
  if (!recentBooks.empty()) {
    const RecentBook& front = recentBooks.front();
    if (front.path == path && front.title == title && front.author == author && front.coverBmpPath == coverBmpPath) {
      return;
    }
  }

  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  // Remove existing entry if present, carrying its reading progress across the
  // move-to-front so reopening a book doesn't wipe its [NN%] badge.
  int preservedProgress = -1;
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    preservedProgress = it->progressPercent;
    recentBooks.erase(it);
  }

  // Add to front
  RecentBook book{path, title, author, coverBmpPath};
  book.progressPercent = preservedProgress;
  recentBooks.insert(recentBooks.begin(), book);

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  ensureLoaded();
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

void RecentBooksStore::setProgress(const std::string& path, int percent) {
  ensureLoaded();
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return;
  }
  const int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  if (it->progressPercent == clamped) {
    return;  // unchanged — skip the SD write
  }
  it->progressPercent = clamped;
  saveToFile();
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  ensureLoaded();
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  ensureLoaded();
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

std::string RecentBooksStore::relocateOpenedBookToRecents(const std::string& bookPath) {
  using namespace book_relocation;

  if (SETTINGS.moveOpenedToRecents == 0) return bookPath;

  // Only real book types have a hash-keyed cache dir; skip images/unknown types.
  const std::string oldCache = bookCacheDir(bookPath);
  if (oldCache.empty()) return bookPath;

  // Already in /recents/ — nothing to do.
  if (isUnderRecents(bookPath)) return bookPath;

  Storage.mkdir(RECENTS_DIR);

  // Destination keeps the filename; uniquify only against a DIFFERENT existing
  // file so a same-named book from another folder does not clobber the first.
  std::string dest = recentsDestPath(bookPath);
  if (Storage.exists(dest.c_str())) {
    const size_t dotPos = dest.rfind('.');
    const std::string base = (dotPos != std::string::npos) ? dest.substr(0, dotPos) : dest;
    const std::string ext = (dotPos != std::string::npos) ? dest.substr(dotPos) : "";
    int suffix = 2;
    do {
      dest = base + " (" + std::to_string(suffix) + ")" + ext;
      suffix++;
    } while (Storage.exists(dest.c_str()) && suffix < 100);
  }

  // Move the book file first. On failure leave everything in place (no data loss).
  if (!Storage.rename(bookPath.c_str(), dest.c_str())) {
    LOG_ERR("RBS", "Move to recents failed: %s -> %s", bookPath.c_str(), dest.c_str());
    return bookPath;
  }

  // Relocate the hash-keyed cache dir so the reading position + rendered sections
  // survive (the cache dir name is derived from the file path hash).
  const std::string newCache = bookCacheDir(dest);
  if (Storage.exists(oldCache.c_str()) && !Storage.rename(oldCache.c_str(), newCache.c_str())) {
    LOG_ERR("RBS", "Cache re-key failed (progress may reset): %s -> %s", oldCache.c_str(), newCache.c_str());
  }

  // Carry the saved-quotes sidecar along, if the book has one.
  const std::string oldQuotes = quotesSidecarPath(bookPath);
  const std::string newQuotes = quotesSidecarPath(dest);
  if (Storage.exists(oldQuotes.c_str()) && !Storage.rename(oldQuotes.c_str(), newQuotes.c_str())) {
    LOG_ERR("RBS", "Quotes sidecar move failed: %s -> %s", oldQuotes.c_str(), newQuotes.c_str());
  }

  // Repoint an existing recents entry (a resumed book) and the resume pointer.
  updatePath(bookPath, dest, oldCache, newCache);
  if (APP_STATE.openEpubPath == bookPath) {
    APP_STATE.openEpubPath = dest;
    APP_STATE.saveToFile();
  }

  LOG_INF("RBS", "Moved book to recents: %s -> %s", bookPath.c_str(), dest.c_str());
  return dest;
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  ensureLoaded();
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

bool RecentBooksStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveRecentBooks(*this, RECENT_BOOKS_FILE_JSON);
}

void RecentBooksStore::restoreMissingProgress() {
  // In-RAM only: no saveToFile here (this runs on the lazy first load, i.e.
  // usually the wake path). The recovered value persists with the next
  // ordinary store write. At most MAX_RECENT_BOOKS tiny sidecar reads, and
  // only for entries whose badge is actually missing.
  for (RecentBook& book : recentBooks) {
    if (book.progressPercent >= 0) continue;
    const int pct = readSidecarPercent(book.path);
    if (pct >= 0) book.progressPercent = pct;
  }
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  // Set before any I/O: a missing/corrupt file must read as a loaded-but-empty
  // store, not retrigger the SD read on every subsequent access.
  loaded = true;
  // Try JSON first
  if (Storage.exists(RECENT_BOOKS_FILE_JSON)) {
    String json = Storage.readFile(RECENT_BOOKS_FILE_JSON);
    if (!json.isEmpty()) {
      const bool ok = JsonSettingsIO::loadRecentBooks(*this, json.c_str());
      if (ok) restoreMissingProgress();
      return ok;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(RECENT_BOOKS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      restoreMissingProgress();
      saveToFile();
      Storage.rename(RECENT_BOOKS_FILE_BIN, RECENT_BOOKS_FILE_BAK);
      LOG_DBG("RBS", "Migrated recent.bin to recent.json");
      return true;
    }
  }

  return false;
}

bool RecentBooksStore::loadFromBinaryFile() {
  HalFile inputFile;
  if (!Storage.openFileForRead("RBS", RECENT_BOOKS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version == 1 || version == 2) {
    // Old version, just read paths
    uint8_t count;
    serialization::readPod(inputFile, count);
    const uint8_t safeCount = std::min<uint8_t>(count, MAX_RECENT_BOOKS);
    recentBooks.clear();
    recentBooks.reserve(safeCount);
    for (uint8_t i = 0; i < safeCount; i++) {
      std::string path;
      if (!serialization::readString(inputFile, path)) return false;

      // load book to get missing data
      RecentBook book = getDataFromBook(path);
      if (book.title.empty() && book.author.empty() && version == 2) {
        // Fall back to loading what we can from the store
        std::string title, author;
        if (!serialization::readString(inputFile, title) || !serialization::readString(inputFile, author)) {
          return false;
        }
        recentBooks.push_back({path, title, author, ""});
      } else {
        recentBooks.push_back(book);
      }
    }
  } else if (version == 3) {
    uint8_t count;
    serialization::readPod(inputFile, count);
    const uint8_t safeCount = std::min<uint8_t>(count, MAX_RECENT_BOOKS);

    recentBooks.clear();
    recentBooks.reserve(safeCount);
    uint8_t omitted = 0;

    for (uint8_t i = 0; i < safeCount; i++) {
      std::string path, title, author, coverBmpPath;
      if (!serialization::readString(inputFile, path) || !serialization::readString(inputFile, title) ||
          !serialization::readString(inputFile, author) || !serialization::readString(inputFile, coverBmpPath)) {
        return false;
      }

      // Omit books with missing title (e.g. saved before metadata was available)
      if (title.empty()) {
        omitted++;
        continue;
      }

      recentBooks.push_back({path, title, author, coverBmpPath});
    }

    if (omitted > 0) {
      // Explicitly close() file before saveToFile() rewrites the same file
      inputFile.close();
      saveToFile();
      LOG_DBG("RBS", "Omitted %u recent book(s) with missing title", omitted);
      return true;
    }
  } else {
    LOG_ERR("RBS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  LOG_DBG("RBS", "Recent books loaded from binary file (%d entries)", static_cast<int>(recentBooks.size()));
  return true;
}
