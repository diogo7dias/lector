#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

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

// Join words with single spaces, suppressing the space before attaching
// punctuation. Hard-capped at maxLen bytes.
inline std::string joinQuoteWords(const std::vector<std::string>& words, const size_t maxLen = MAX_QUOTE_LENGTH) {
  std::string out;
  for (size_t i = 0; i < words.size(); i++) {
    if (i > 0 && !wordAttachesLeft(words[i].c_str())) out.push_back(' ');
    out.append(words[i]);
    if (out.size() >= maxLen) {
      out.resize(maxLen);
      break;
    }
  }
  return out;
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
inline void splitChapterAnchor(const std::string& field, std::string& chapterOut, std::string& anchorOut) {
  chapterOut = field;
  anchorOut.clear();

  const auto at = field.rfind(" @q1:");
  if (at == std::string::npos) return;

  const std::string candidate = field.substr(at + 1);
  QuoteAnchor probe;
  if (!parseAnchorToken(candidate, probe)) return;

  chapterOut = field.substr(0, at);
  anchorOut = candidate;
}

// Form feed, the ASCII page break. Written at the head of every entry so the
// sidecar opened as a book in the TXT reader starts each quote on its own page
// (see TxtReaderActivity::loadPageAtOffset). Files written before this change
// have none and simply flow as they always did.
inline constexpr char PAGE_BREAK = '\f';

// True for the bytes that separate records: real whitespace plus the page break.
// Every parser of this file must skip these before looking for a header bracket.
inline bool isRecordGap(const char c) { return c == '\n' || c == '\r' || c == ' ' || c == PAGE_BREAK; }

// One sidecar entry: "\f[chapter @q1:...]\nquote\n---\n\n". The anchor lives inside
// the brackets on purpose: the record grammar (bracketed header line, body, "---")
// is unchanged, so a reader that knows nothing about anchors still parses every
// record. An empty token writes no anchor at all.
inline std::string formatQuoteEntry(const std::string& chapter, const std::string& anchorToken,
                                    const std::string& quote) {
  std::string entry;
  entry.reserve(chapter.size() + anchorToken.size() + quote.size() + 14);
  entry.push_back(PAGE_BREAK);
  entry.push_back('[');
  entry.append(chapter);
  if (!anchorToken.empty()) {
    entry.push_back(' ');
    entry.append(anchorToken);
  }
  entry.append("]\n");
  entry.append(quote);
  entry.append("\n---\n\n");
  return entry;
}

// Anchorless overload, kept so callers that have no position to record stay put.
inline std::string formatQuoteEntry(const std::string& chapter, const std::string& quote) {
  return formatQuoteEntry(chapter, "", quote);
}

}  // namespace quote_text
