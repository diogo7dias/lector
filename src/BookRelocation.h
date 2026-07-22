#pragma once

#include <cctype>
#include <string>

// Pure, dependency-free path helpers for relocating an opened book into the
// flat /recents/ folder (the "move opened books to Recents" feature). Kept free
// of HalStorage/FsHelpers so the fiddly string logic is host-unit-testable
// (test/book_relocation). The Storage-touching move itself, plus the hash-keyed
// cache-dir relocation that preserves reading progress, lives in
// RecentBooksStore::relocateOpenedBookToRecents.
namespace book_relocation {

// The single flat folder every opened book is relocated into.
constexpr char RECENTS_DIR[] = "/recents";

// Portion of a path after the last '/', or the whole string if there is none.
inline std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Destination path in /recents/ for a book (keeps the original filename).
inline std::string recentsDestPath(const std::string& bookPath) {
  return std::string(RECENTS_DIR) + "/" + baseName(bookPath);
}

// True if the path already lives directly under the /recents/ root
// (case-insensitive), so a re-open is a no-op instead of a redundant move.
inline bool isUnderRecents(const std::string& path) {
  std::string lower = path;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.rfind("/recents/", 0) == 0;
}

// The <book>_QUOTES.txt sidecar path (extension stripped, then suffix appended),
// matching EpubReaderActivity::getQuotesFilePath so the move carries a book's
// saved quotes along with it instead of orphaning them.
inline std::string quotesSidecarPath(const std::string& bookPath) {
  const size_t dot = bookPath.rfind('.');
  const std::string base = (dot == std::string::npos) ? bookPath : bookPath.substr(0, dot);
  return base + "_QUOTES.txt";
}

}  // namespace book_relocation
