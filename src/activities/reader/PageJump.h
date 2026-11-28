#pragma once

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>

// Pure helpers for Go to Page in chapter-page mode (EPUBs without a printed page list).
// Kept free of reader and Arduino headers so host tests exercise every edge case.
namespace page_jump {

// Parse a 1-based chapter page number against `totalPages` (> 0).
// Trims ASCII spaces/tabs; rejects empty input, signs, non-digits, 0, overflow, or > totalPages.
// Returns the 0-based page index on success.
inline std::optional<int> parseChapterPage(std::string_view text, const int totalPages) {
  if (totalPages <= 0) return std::nullopt;
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  if (text.empty()) return std::nullopt;

  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    const int digit = c - '0';
    if (value > (totalPages - digit) / 10) return std::nullopt;
    value = value * 10 + digit;
  }
  if (value < 1 || value > totalPages) return std::nullopt;
  return value - 1;
}

// Formats the keyboard title for chapter-page entry: "<baseLabel> (N/M)" when M > 0, else "<baseLabel>".
inline void formatChapterTitle(char* out, const size_t outSize, const char* baseLabel, const int currentPage1Based,
                               const int totalPages) {
  if (!out || outSize == 0) return;
  const char* label = baseLabel ? baseLabel : "";
  if (totalPages > 0) {
    const int shown = currentPage1Based < 1 ? 1 : (currentPage1Based > totalPages ? totalPages : currentPage1Based);
    std::snprintf(out, outSize, "%s (%d/%d)", label, shown, totalPages);
  } else {
    std::snprintf(out, outSize, "%s", label);
  }
}

}  // namespace page_jump
