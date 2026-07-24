#pragma once

#include <cstddef>
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

// One sidecar entry: "[chapter]\nquote\n---\n\n".
inline std::string formatQuoteEntry(const std::string& chapter, const std::string& quote) {
  std::string entry;
  entry.reserve(chapter.size() + quote.size() + 12);
  entry.push_back('[');
  entry.append(chapter);
  entry.append("]\n");
  entry.append(quote);
  entry.append("\n---\n\n");
  return entry;
}

}  // namespace quote_text
