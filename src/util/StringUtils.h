#pragma once

#include <cctype>
#include <string>

namespace StringUtils {

/**
 * Case-insensitive ASCII strcmp. Returns <0, 0, or >0 like strcmp, comparing
 * each byte by its lowercased value.
 *
 * Used wherever data is sorted case-insensitively: StarDict indexes (including
 * wiktionary-derived dictionaries) and the on-disk dictionary folder list.
 * Plain strcmp would land a binary search on the wrong page for any word whose
 * alphabetic neighbourhood contains mixed-case boundaries.
 *
 * Inline (header) because Dictionary's binary search calls it per comparison
 * step; a cross-TU call here would defeat inlining on a hot path.
 */
inline int asciiCaseCmp(const char* a, const char* b) {
  while (*a && *b) {
    int diff = std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
    if (diff != 0) return diff;
    ++a;
    ++b;
  }
  return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}

/**
 * Sanitize a string for use as a filename.
 * Replaces invalid characters with underscores, trims spaces/dots,
 * and limits length to maxBytes bytes.
 */
std::string sanitizeFilename(const std::string& name, size_t maxBytes = 100);

/**
 * Up to `maxInitials` leading letters, one per whitespace-separated word of an
 * author name, uppercased (ASCII). "Ursula K. Le Guin" -> "UKLG",
 * "J.R.R. Tolkien" -> "JT". Returns empty for an empty/blank author. ASCII-only:
 * a multi-byte UTF-8 leading byte is passed through unchanged (not uppercased),
 * which is safe. Header-inline: the home list builds it per visible row.
 */
inline std::string authorInitials(const std::string& author, size_t maxInitials = 4) {
  std::string initials;
  bool newWord = true;
  for (const char ch : author) {
    if (ch == ' ' || ch == '\t') {
      newWord = true;
      continue;
    }
    if (newWord) {
      if (ch >= 'a' && ch <= 'z') {
        initials.push_back(static_cast<char>(ch - ('a' - 'A')));
      } else {
        initials.push_back(ch);
      }
      if (initials.size() >= maxInitials) break;
      newWord = false;
    }
  }
  return initials;
}

}  // namespace StringUtils
