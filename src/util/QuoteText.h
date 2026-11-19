#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

// Pure, Arduino-free helpers for the Grab Quote feature so they can be host
// tested. The interactive selection and the SD read-modify-write live in
// QuoteSelectActivity; only the string/format logic lives here.
namespace quote_text {

// Cap for a single joined quote (bytes). Matches the DX34 heritage limit.
inline constexpr size_t MAX_QUOTE_LENGTH = 8192;

// Cap for the whole "<book>_QUOTES.txt" sidecar (bytes). A save that would
// exceed it is refused rather than growing the file unbounded on the SD card.
inline constexpr size_t MAX_QUOTES_FILE_BYTES = 24 * 1024;

// "<book>.epub" -> "<book>_QUOTES.txt" (strips the last extension). A path with
// no extension gets the suffix appended. Mirrors the old fork's getQuotesFilePath.
inline std::string quotesFilePathFor(const std::string& bookPath) {
  const auto dot = bookPath.rfind('.');
  const std::string base = (dot != std::string::npos) ? bookPath.substr(0, dot) : bookPath;
  return base + "_QUOTES.txt";
}

// A token that begins with closing/attaching punctuation joins to the previous
// word with no leading space (", . ; : ! ? ) ").
inline bool wordAttachesLeft(const char* word) {
  if (!word || word[0] == '\0') return false;
  switch (word[0]) {
    case ',':
    case '.':
    case ';':
    case ':':
    case '!':
    case '?':
    case ')':
    case '"':
      return true;
    default:
      return false;
  }
}

// Append one word to a quote being built, with the single-space rule above.
// Returns false and leaves `out` untouched when the word would pass maxLen, so a
// caller building a quote across several pages can stop cleanly at the cap
// instead of saving a passage cut mid-word.
inline bool appendQuoteWord(std::string& out, const char* word, const size_t maxLen = MAX_QUOTE_LENGTH) {
  if (!word) return false;
  const bool needsSpace = !out.empty() && !wordAttachesLeft(word);
  const size_t grown = out.size() + (needsSpace ? 1 : 0) + std::strlen(word);
  if (grown > maxLen) return false;
  if (needsSpace) out.push_back(' ');
  out.append(word);
  return true;
}

// Where a quote sits in the book, so the reader can underline it again on a
// later visit. Position is NOT stored as coordinates: a relayout (font size,
// margins, orientation) moves every word, so the durable part is the chapter
// plus the paragraph the quote starts in. The saved quote text itself is what
// pins the exact words down at draw time (see QuoteUnderline.h).
//
// `paragraph` is the chapter-local PageLine::paragraphOrdinal; 0 means "not
// derivable from the page it was grabbed on" and the reader then falls back to
// matching anywhere in the chapter. `wordHint` is the index the quote started
// at on its original page: a tie-break for a passage repeated verbatim, never
// trusted on its own.
struct QuoteAnchor {
  uint16_t spine = 0;
  uint16_t paragraph = 0;
  uint16_t wordHint = 0;
  bool valid = false;
};

// The anchor as it appears inside the header brackets: "@q1:<spine>,<para>,<hint>".
// Versioned ("q1") so a later format can be told apart from this one.
inline std::string formatAnchorToken(const QuoteAnchor& anchor) {
  if (!anchor.valid) return "";
  return "@q1:" + std::to_string(anchor.spine) + "," + std::to_string(anchor.paragraph) + "," +
         std::to_string(anchor.wordHint);
}

// Parse "@q1:12,37,84". Anything else leaves `out` untouched and returns false.
inline bool parseAnchorToken(const std::string& token, QuoteAnchor& out) {
  static constexpr char PREFIX[] = "@q1:";
  constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
  if (token.compare(0, PREFIX_LEN, PREFIX) != 0) return false;

  uint32_t fields[3] = {0, 0, 0};
  size_t pos = PREFIX_LEN;
  for (int f = 0; f < 3; f++) {
    if (f > 0) {
      if (pos >= token.size() || token[pos] != ',') return false;
      pos++;
    }
    const size_t digitsStart = pos;
    while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') {
      fields[f] = fields[f] * 10 + static_cast<uint32_t>(token[pos] - '0');
      if (fields[f] > 65535) return false;  // would not survive the uint16 fields
      pos++;
    }
    if (pos == digitsStart) return false;  // no digits at all
  }
  if (pos != token.size()) return false;  // trailing junk

  out.spine = static_cast<uint16_t>(fields[0]);
  out.paragraph = static_cast<uint16_t>(fields[1]);
  out.wordHint = static_cast<uint16_t>(fields[2]);
  out.valid = true;
  return true;
}

// Split a header field ("Chapter Title @q1:12,37,84") into its title and its
// anchor token. The scan is right-anchored on the last " @q1:" so a chapter
// title is only ever shortened when the tail really parses as an anchor.
// Records written before anchors existed have no token and pass through whole.
// What a record knows beyond its chapter and anchor, kept as " @o:<n>" and " @t:<n>"
// tokens just before the anchor. Records without them read back as unknown.
struct QuoteMeta {
  // UTC save time in seconds; 0 when the device had no clock.
  uint32_t savedAt = 0;
  // Visible-text offset in the chapter of the page the quote starts on, in the units
  // KOReader sync counts (ChapterXPathResolver); NO_OFFSET when not recorded.
  static constexpr uint32_t NO_OFFSET = UINT32_MAX;
  uint32_t textOffset = NO_OFFSET;
};

inline void splitChapterAnchor(const std::string& field, std::string& chapterOut, std::string& anchorOut,
                               QuoteMeta* metaOut = nullptr) {
  chapterOut = field;
  anchorOut.clear();
  if (metaOut) *metaOut = QuoteMeta{};

  const auto at = field.rfind(" @q1:");
  if (at != std::string::npos) {
    const std::string candidate = field.substr(at + 1);
    QuoteAnchor probe;
    if (parseAnchorToken(candidate, probe)) {
      chapterOut = field.substr(0, at);
      anchorOut = candidate;
    }
  }

  // Trailing " @t:<digits>" / " @o:<digits>" tokens, in any order.
  while (true) {
    const auto tok = chapterOut.rfind(" @");
    if (tok == std::string::npos || tok + 4 >= chapterOut.size() || chapterOut[tok + 3] != ':') return;
    const char key = chapterOut[tok + 2];
    if (key != 't' && key != 'o') return;
    uint64_t value = 0;
    for (size_t i = tok + 4; i < chapterOut.size(); i++) {
      if (chapterOut[i] < '0' || chapterOut[i] > '9') return;
      value = value * 10 + static_cast<uint64_t>(chapterOut[i] - '0');
      if (value >= UINT32_MAX) return;
    }
    if (metaOut) (key == 't' ? metaOut->savedAt : metaOut->textOffset) = static_cast<uint32_t>(value);
    chapterOut.erase(tok);
  }
}

// Form feed, the ASCII page break. Written at the head of every entry so the
// sidecar opened as a book in the TXT reader starts each quote on its own page
// (see TxtReaderActivity::loadPageAtOffset). Files written before this change
// have none and simply flow as they always did.
inline constexpr char PAGE_BREAK = '\f';

// True for the bytes that separate records: real whitespace plus the page break.
// Every parser of this file must skip these before looking for a header bracket.
inline bool isRecordGap(const char c) { return c == '\n' || c == '\r' || c == ' ' || c == PAGE_BREAK; }

// One record of the sidecar, located in place so no parser copies what it skips:
// the bracketed header field (chapter title plus any anchor token), if the record has
// one, and the quote text up to, not including, its "\n---" separator.
struct QuoteRecord {
  bool hasHeader = false;
  size_t headerStart = 0;
  size_t headerLen = 0;
  size_t textStart = 0;
  size_t textEnd = 0;
};

// The one reader of the record grammar below. Parses the record at `pos` and moves
// `pos` past it; false when only record gaps are left. A '[' with no closing ']' is
// not a header, so that record's text starts at the bracket.
inline bool nextQuoteRecord(const std::string& buf, size_t& pos, QuoteRecord& out) {
  while (pos < buf.size() && isRecordGap(buf[pos])) ++pos;
  if (pos >= buf.size()) return false;

  out = QuoteRecord{};
  if (buf[pos] == '[') {
    const auto close = buf.find(']', pos);
    if (close != std::string::npos) {
      out.hasHeader = true;
      out.headerStart = pos + 1;
      out.headerLen = close - pos - 1;
      pos = close + 1;
      while (pos < buf.size() && (buf[pos] == '\n' || buf[pos] == '\r')) ++pos;
    }
  }
  out.textStart = pos;
  const auto sep = buf.find("\n---", pos);
  out.textEnd = (sep == std::string::npos) ? buf.size() : sep;
  pos = (sep == std::string::npos) ? buf.size() : sep + 4;
  return true;
}

// One sidecar entry: "\f[chapter @q1:...]\nquote\n---\n\n". The anchor lives inside
// the brackets on purpose: the record grammar (bracketed header line, body, "---")
// is unchanged, so a reader that knows nothing about anchors still parses every
// record. An empty token writes no anchor at all.
inline std::string formatQuoteEntry(const std::string& chapter, const std::string& anchorToken,
                                    const std::string& quote, const QuoteMeta& meta = {}) {
  std::string entry;
  entry.reserve(chapter.size() + anchorToken.size() + quote.size() + 40);
  entry.push_back(PAGE_BREAK);
  entry.push_back('[');
  entry.append(chapter);
  if (meta.textOffset != QuoteMeta::NO_OFFSET) {
    entry.append(" @o:");
    entry.append(std::to_string(meta.textOffset));
  }
  if (meta.savedAt != 0) {
    entry.append(" @t:");
    entry.append(std::to_string(meta.savedAt));
  }
  if (!anchorToken.empty()) {
    entry.push_back(' ');
    entry.append(anchorToken);
  }
  entry.append("]\n");
  entry.append(quote);
  entry.append("\n---\n\n");
  return entry;
}

// A stand-in save time for a quote kept without one (saved before quotes carried a time,
// or with no clock). Fixed per text, so every sync sends the same value and BookOrbit,
// which keys a highlight on its time and position, never counts it twice.
// ponytail: dated in 2020; store a real time when the clock knows it (all new saves).
inline uint32_t fallbackSavedAt(const std::string& quote) {
  uint32_t hash = 2166136261u;
  for (const char c : quote) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  return 1577836800u + hash % (365u * 86400u);
}

// "2026-11-19 22:05:07", the local wall-clock form KOReader and BookOrbit use.
inline std::string koreaderDatetime(const int64_t localSeconds) {
  const time_t t = static_cast<time_t>(localSeconds);
  struct tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];  // sized for any int the compiler can imagine, not just real dates
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
           tm.tm_min, tm.tm_sec);
  return buf;
}

// The Kindle "My Clippings.txt" file at the card root, which Readwise, Calibre and
// most clipping tools import. Every save appends one entry; the book's own quotes
// file stays the one the reader and the viewer use.
inline constexpr char CLIPPINGS_PATH[] = "/My Clippings.txt";

// One Kindle entry, laid out as CrossPoint writes it:
//   Title (Author)
//   - Your Highlight on Page 12 | Chapter | Added on Wednesday, November 19, 2026, 10:05 PM
//
//   quote
//   ==========
// `localSeconds` 0 leaves "Added on" out (no clock).
inline std::string formatClipping(const std::string& title, const std::string& author, const std::string& chapter,
                                  const int page, const std::string& quote, const int64_t localSeconds) {
  static constexpr const char* WEEKDAYS[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                             "Thursday", "Friday", "Saturday"};
  static constexpr const char* MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                           "July",    "August",   "September", "October", "November", "December"};
  std::string entry = title + " (" + author + ")\n- Your Highlight on Page " + std::to_string(page);
  if (!chapter.empty()) entry += " | " + chapter;
  if (localSeconds != 0) {
    const time_t t = static_cast<time_t>(localSeconds);
    struct tm tm{};
    gmtime_r(&t, &tm);
    const int hour12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
    char added[80];
    snprintf(added, sizeof(added), " | Added on %s, %s %d, %d, %02d:%02d %s", WEEKDAYS[tm.tm_wday], MONTHS[tm.tm_mon],
             tm.tm_mday, tm.tm_year + 1900, hour12, tm.tm_min, tm.tm_hour >= 12 ? "PM" : "AM");
    entry += added;
  }
  entry += "\n\n" + quote + "\n==========\n";
  return entry;
}

}  // namespace quote_text
