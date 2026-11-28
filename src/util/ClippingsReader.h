#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

// Pure, Arduino-free parser and O(1) seeker for Kindle-format "/My Clippings.txt".
// Shared by the Quote of the Day sleep face (B2) and Sortes Flashback (B12).
namespace clippings {

inline constexpr size_t MAX_WINDOW_BYTES = 4096;
inline constexpr size_t MAX_TITLE_BYTES = 160;
inline constexpr size_t MAX_AUTHOR_BYTES = 120;
inline constexpr size_t MAX_QUOTE_BYTES = 1024;
inline constexpr std::string_view SEPARATOR = "==========";

struct Record {
  std::string title;
  std::string author;
  std::string quote;
};

// Reads up to `len` bytes starting at `offset` into `dst`. Returns bytes read (0 on EOF/error).
using ReadAtFn = size_t (*)(void* ctx, uint64_t offset, char* dst, size_t len);

// Stable 64-bit FNV-1a hash of a local calendar day (YYYY-MM-DD).
inline uint64_t dailySeed(const int year, const int month, const int day) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
  uint64_t h = 14695981039346656037ULL;
  for (int i = 0; i < n; ++i) {
    h ^= static_cast<unsigned char>(buf[i]);
    h *= 1099511628211ULL;
  }
  return h;
}

inline uint64_t offsetForSeed(const uint64_t seed, const uint64_t fileSize) {
  return fileSize == 0 ? 0 : (seed % fileSize);
}

inline std::string_view trimView(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

inline std::string_view stripUtf8Bom(std::string_view s) {
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF) {
    s.remove_prefix(3);
  }
  return s;
}

// Truncates UTF-8 safely to at most `maxBytes` without splitting a multi-byte sequence.
inline std::string boundedUtf8(std::string_view in, const size_t maxBytes) {
  if (in.size() <= maxBytes) return std::string(in);
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<unsigned char>(in[cut]) & 0xC0) == 0x80) --cut;
  return std::string(in.substr(0, cut));
}

// Parses one clipping record (the text between two "==========" lines).
// Format:
//   Title (Author)
//   - Your Highlight ...
//   <blank line>
//   Quote text
// Returns false when the record has no non-empty quote body (e.g. a bookmark).
inline bool parseRecord(std::string_view raw, Record& out) {
  out = {};
  raw = trimView(stripUtf8Bom(raw));
  if (raw.empty()) return false;

  const size_t nl1 = raw.find('\n');
  if (nl1 == std::string_view::npos) return false;
  std::string_view header = trimView(stripUtf8Bom(raw.substr(0, nl1)));
  if (header.empty()) return false;

  // Split trailing "(Author)" if present.
  std::string_view titleView = header;
  std::string_view authorView;
  if (header.size() >= 3 && header.back() == ')') {
    const size_t openParen = header.rfind('(');
    if (openParen != std::string_view::npos && openParen > 0) {
      authorView = trimView(header.substr(openParen + 1, header.size() - openParen - 2));
      titleView = trimView(header.substr(0, openParen));
    }
  }

  // Skip the metadata line ("- Your Highlight ...").
  std::string_view rest = raw.substr(nl1 + 1);
  const size_t nl2 = rest.find('\n');
  if (nl2 == std::string_view::npos) return false;
  std::string_view body = trimView(rest.substr(nl2 + 1));
  if (body.empty()) return false;

  out.title = boundedUtf8(titleView, MAX_TITLE_BYTES);
  out.author = boundedUtf8(authorView, MAX_AUTHOR_BYTES);
  out.quote = boundedUtf8(body, MAX_QUOTE_BYTES);
  return !out.quote.empty();
}

// Finds the first valid clipping record inside `window` starting at `startPos`.
// When `skipPartialFirst` is true, advances past the first "==========" first.
inline bool parseNextInWindow(const std::string_view window, const bool skipPartialFirst, Record& out) {
  size_t pos = 0;
  if (skipPartialFirst) {
    const size_t sep = window.find(SEPARATOR);
    if (sep == std::string_view::npos) return false;
    pos = sep + SEPARATOR.size();
    while (pos < window.size() && (window[pos] == '\r' || window[pos] == '\n')) ++pos;
  }

  while (pos < window.size()) {
    const size_t sep = window.find(SEPARATOR, pos);
    const std::string_view slice = sep == std::string_view::npos ? window.substr(pos) : window.substr(pos, sep - pos);
    // If there is no closing separator and we did not reach the true end of the file,
    // wait — callers pass `atEof` via parseRecord; if a partial tail has a full quote body
    // it still parses cleanly, and short files without trailing separator also work.
    if (parseRecord(slice, out)) return true;
    if (sep == std::string_view::npos) break;
    pos = sep + SEPARATOR.size();
    while (pos < window.size() && (window[pos] == '\r' || window[pos] == '\n')) ++pos;
  }
  return false;
}

// Seeks to `seekOffset`, scans past the next "==========", and parses one record.
// Wraps around to offset 0 when `seekOffset` lands in or after the last record.
inline bool pickAtOffset(const uint64_t fileSize, const uint64_t seekOffset, ReadAtFn readAt, void* ctx, Record& out) {
  out = {};
  if (fileSize == 0 || !readAt) return false;

  auto buf = makeUniqueNoThrow<char[]>(MAX_WINDOW_BYTES);
  if (!buf) return false;

  const uint64_t start = seekOffset % fileSize;
  if (start > 0) {
    const size_t want = static_cast<size_t>(std::min<uint64_t>(MAX_WINDOW_BYTES, fileSize - start));
    const size_t got = readAt(ctx, start, buf.get(), want);
    if (got > 0 && parseNextInWindow(std::string_view(buf.get(), got), /*skipPartialFirst=*/true, out)) {
      return true;
    }
  }

  // Wrap around to the start of the file (covers offset == 0 and last-record wrap).
  const size_t want = static_cast<size_t>(std::min<uint64_t>(MAX_WINDOW_BYTES, fileSize));
  const size_t got = readAt(ctx, 0, buf.get(), want);
  if (got == 0) return false;
  return parseNextInWindow(std::string_view(buf.get(), got), /*skipPartialFirst=*/false, out);
}

}  // namespace clippings
