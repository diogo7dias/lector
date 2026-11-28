#pragma once

// Pure helpers for End-of-Book next-book matching (LibraryIndexFile fallback) and
// action-row mapping. Free of Arduino/HalStorage so they are host-tested.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/BookFilingNames.h"

namespace end_of_book {

inline constexpr size_t MAX_SUGGESTIONS = 3;
inline constexpr size_t MAX_ACTION_ROWS = MAX_SUGGESTIONS + 3;  // books + Sync + View Quotes + Home
inline constexpr size_t MIN_PREFIX_LEN = 4;

enum class RowKind : uint8_t {
  Book,
  SyncProgress,
  ViewQuotes,
  Home,
};

struct ActionRow {
  RowKind kind = RowKind::Home;
  int bookIndex = -1;
};

struct ActionRows {
  std::array<ActionRow, MAX_ACTION_ROWS> items{};
  int count = 0;

  bool empty() const { return count == 0; }
  const ActionRow& operator[](const int i) const { return items[static_cast<size_t>(i)]; }
};

struct LibraryCandidate {
  std::string displayName;
  std::string fullPath;
  uint8_t priority = 1;  // 0 = same author, 1 = filename prefix
};

inline bool isAsciiSpace(const unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline std::string_view trimAscii(std::string_view sv) {
  while (!sv.empty() && isAsciiSpace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
  while (!sv.empty() && isAsciiSpace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
  return sv;
}

inline char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool isSameAuthor(std::string_view a, std::string_view b) {
  a = trimAscii(a);
  b = trimAscii(b);
  if (a.empty() || b.empty() || a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
  }
  return true;
}

inline std::string_view filenameStem(std::string_view path) {
  const std::string_view name = bookfiling::fileNameOf(path);
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0) return name;
  return name.substr(0, dot);
}

inline bool isPrefixSeparator(const char c) {
  return c == ' ' || c == '-' || c == '_' || c == '.' || c == '#' || c == ':' || c == '(' || c == '[';
}

inline bool endsWithWordCI(std::string_view s, std::string_view word) {
  if (s.size() < word.size()) return false;
  const size_t start = s.size() - word.size();
  if (start > 0 && !isPrefixSeparator(s[start - 1])) return false;
  for (size_t i = 0; i < word.size(); ++i) {
    if (lowerAscii(s[start + i]) != word[i]) return false;
  }
  return true;
}

// Normalizes a book filename stem into a lowercase series/prefix key by stripping
// trailing volume numbers and markers ("Dune 01", "Dune - Book 2", "Foundation_3").
inline std::string extractSeriesPrefix(std::string_view path) {
  std::string_view stem = trimAscii(filenameStem(path));
  std::string out;
  out.reserve(stem.size());
  for (const char c : stem) out.push_back(lowerAscii(c));

  auto trimTail = [&]() {
    while (!out.empty() && isPrefixSeparator(out.back())) out.pop_back();
  };

  trimTail();
  // Strip trailing digits (e.g. "dune 01" -> "dune", "book2" -> "book") and optional
  // volume marker word ("book", "vol", "volume", "part", "pt", "no", "ch") before them.
  bool strippedDigits = false;
  while (!out.empty() && std::isdigit(static_cast<unsigned char>(out.back())) != 0) {
    out.pop_back();
    strippedDigits = true;
  }
  if (strippedDigits) {
    trimTail();
    static constexpr std::string_view kMarkers[] = {"volume", "vol", "book", "part", "pt", "no", "bk"};
    for (const auto marker : kMarkers) {
      if (endsWithWordCI(out, marker)) {
        out.resize(out.size() - marker.size());
        trimTail();
        break;
      }
    }
  }
  return out;
}

// True when candidatePath's stem starts with currentPath's series prefix (length >= 4)
// followed by a separator, digit, or end of stem.
inline bool matchesFilenamePrefix(std::string_view currentPath, std::string_view candidatePath) {
  const std::string prefix = extractSeriesPrefix(currentPath);
  if (prefix.size() < MIN_PREFIX_LEN) return false;

  const std::string_view rawCand = trimAscii(filenameStem(candidatePath));
  if (rawCand.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (lowerAscii(rawCand[i]) != prefix[i]) return false;
  }
  if (rawCand.size() == prefix.size()) return true;
  const char next = rawCand[prefix.size()];
  return isPrefixSeparator(next) || std::isdigit(static_cast<unsigned char>(next)) != 0;
}

// True when EndOfBookOptions should query LibraryIndexFile instead of (or after)
// NextBookFinder: either the book was filed into /recents (so sibling files are unrelated
// recents) or NextBookFinder found no sibling books.
inline bool shouldUseLibraryFallback(std::string_view currentBookPath, const size_t siblingCount) {
  return bookfiling::isInFolder(currentBookPath, bookfiling::RECENTS_FOLDER) || siblingCount == 0;
}

// True when a library index entry represents the same book or an already-filed-as-read book
// and must be skipped. Checking filename equality catches books moved into /recents after the
// index was built (still listed under their pre-move folder).
inline bool shouldSkipLibraryEntry(std::string_view currentBookPath, std::string_view candidatePath) {
  if (candidatePath.empty() || candidatePath == currentBookPath) return true;
  if (bookfiling::isInFolder(candidatePath, bookfiling::READ_FOLDER)) return true;
  if (bookfiling::fileNameOf(candidatePath) == bookfiling::fileNameOf(currentBookPath)) return true;
  return false;
}

// Case-insensitive natural ordering (numeric runs compared by value: "2" < "10").
inline bool naturalLess(std::string_view a, std::string_view b) {
  size_t ia = 0;
  size_t ib = 0;
  while (ia < a.size() && ib < b.size()) {
    const unsigned char ca = static_cast<unsigned char>(a[ia]);
    const unsigned char cb = static_cast<unsigned char>(b[ib]);
    if (std::isdigit(ca) != 0 && std::isdigit(cb) != 0) {
      while (ia < a.size() && a[ia] == '0') ++ia;
      while (ib < b.size() && b[ib] == '0') ++ib;
      const size_t sa = ia;
      const size_t sb = ib;
      while (ia < a.size() && std::isdigit(static_cast<unsigned char>(a[ia])) != 0) ++ia;
      while (ib < b.size() && std::isdigit(static_cast<unsigned char>(b[ib])) != 0) ++ib;
      const size_t la = ia - sa;
      const size_t lb = ib - sb;
      if (la != lb) return la < lb;
      for (size_t k = 0; k < la; ++k) {
        if (a[sa + k] != b[sb + k]) return a[sa + k] < b[sb + k];
      }
      continue;
    }
    const char la = lowerAscii(a[ia]);
    const char lb = lowerAscii(b[ib]);
    if (la != lb) return la < lb;
    ++ia;
    ++ib;
  }
  return ia == a.size() && ib < b.size();
}

// Bounded insertion of a matched candidate into `results` (max `maxCount`), keeping
// lower `priority` first (same author before filename prefix) and natural name order
// within each priority tier.
inline void insertCandidate(std::vector<LibraryCandidate>& results, LibraryCandidate candidate,
                            const size_t maxCount = MAX_SUGGESTIONS) {
  if (maxCount == 0) return;
  const auto better = [](const LibraryCandidate& a, const LibraryCandidate& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    return naturalLess(a.displayName, b.displayName);
  };
  if (results.size() >= maxCount && !better(candidate, results.back())) return;
  const auto pos = std::lower_bound(results.begin(), results.end(), candidate, better);
  results.insert(pos, std::move(candidate));
  if (results.size() > maxCount) results.pop_back();
}

// Builds the End-of-Book menu rows in display order:
//   [0..bookCount-1] book suggestions
//   Sync Progress (when configured)
//   View Quotes   (when quotes exist for this book)
//   Home          (always)
inline ActionRows buildActionRows(const int bookCount, const bool canSync, const bool hasQuotes) {
  ActionRows rows;
  const int cappedBooks = std::clamp(bookCount, 0, static_cast<int>(MAX_SUGGESTIONS));
  for (int i = 0; i < cappedBooks; ++i) {
    rows.items[static_cast<size_t>(rows.count++)] = {RowKind::Book, i};
  }
  if (canSync) {
    rows.items[static_cast<size_t>(rows.count++)] = {RowKind::SyncProgress, -1};
  }
  if (hasQuotes) {
    rows.items[static_cast<size_t>(rows.count++)] = {RowKind::ViewQuotes, -1};
  }
  rows.items[static_cast<size_t>(rows.count++)] = {RowKind::Home, -1};
  return rows;
}

}  // namespace end_of_book
