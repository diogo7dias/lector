#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "util/HtmlToPlainText.h"

// Pure helpers for exporting looked-up words to /vocabulary.tsv (Anki-friendly
// tab-separated format: word, headword, snippet, book, date).
namespace vocab_export {

inline constexpr char VOCAB_PATH[] = "/vocabulary.tsv";
inline constexpr size_t MAX_SNIPPET_BYTES = 180;
inline constexpr size_t MAX_FIELD_BYTES = 256;

// Flatten tabs/newlines into single spaces, trim edges, and cap on a UTF-8
// codepoint boundary so no field can ever split or corrupt a TSV row.
inline std::string sanitizeField(const std::string_view raw, const size_t maxBytes = MAX_FIELD_BYTES) {
  std::string out;
  out.reserve(raw.size() < maxBytes ? raw.size() : maxBytes);
  bool prevSpace = true;
  for (const char c : raw) {
    if (c == '\0' || c == '\t' || c == '\r' || c == '\n' || c == ' ') {
      if (!prevSpace) {
        out.push_back(' ');
        prevSpace = true;
      }
    } else {
      out.push_back(c);
      prevSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > maxBytes) {
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) --cut;
    out.erase(cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
  }
  return out;
}

inline std::string makeSnippet(const std::string& definition, const bool isHtml,
                               const size_t maxBytes = MAX_SNIPPET_BYTES) {
  std::string plain = definition;
  for (char& c : plain) {
    if (c == '\0') c = '\n';
  }
  if (isHtml) plain = htmlToPlainText(plain);
  return sanitizeField(plain, maxBytes);
}

inline std::string formatTsvRow(const std::string_view word, const std::string_view headword,
                                const std::string_view snippet, const std::string_view book,
                                const std::string_view date) {
  std::string row;
  row.reserve(word.size() + headword.size() + snippet.size() + book.size() + date.size() + 8);
  row.append(sanitizeField(word));
  row.push_back('\t');
  row.append(sanitizeField(headword));
  row.push_back('\t');
  row.append(sanitizeField(snippet, MAX_SNIPPET_BYTES));
  row.push_back('\t');
  row.append(sanitizeField(book));
  row.push_back('\t');
  row.append(sanitizeField(date));
  row.push_back('\n');
  return row;
}

}  // namespace vocab_export
