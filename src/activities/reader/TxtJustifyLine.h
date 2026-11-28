#pragma once

#include <Epub/JustifySpacing.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace txt_justify {

// Lays out a plain-text line flush to `contentWidth` by distributing spare horizontal
// space across inter-word gaps via JustifySpacing.
//
// `measureWord(const char* word)` returns pixel advance width of a NUL-terminated word.
// `emitWord(int x, const char* word)` receives each word's x offset from the left margin.
//
// Returns true if the line was justified (>= 2 words and positive spare space); returns
// false when the line should be drawn left-aligned as-is (single word, empty, overflow,
// or pathological word count). Uses fixed stack storage so it never heap-allocates in the
// render loop.
template <typename MeasureFn, typename EmitFn>
bool layoutWords(const std::string& line, const int contentWidth, const int spaceWidth, MeasureFn&& measureWord,
                 EmitFn&& emitWord) {
  // ponytail: 48 words a line; a denser line (tiny font, one-letter words) stays left-aligned.
  constexpr size_t MAX_WORDS = 48;
  constexpr size_t MAX_WORD_BUF = 128;

  // Only the widths are kept (96 bytes) and the line is split again to draw, so the
  // frame stays under the 256-byte stack budget on the render task.
  uint16_t widths[MAX_WORDS];
  char wordBuf[MAX_WORD_BUF];
  size_t count = 0;

  const size_t n = line.size();
  size_t pos = 0;
  while (pos < n && line[pos] == ' ') ++pos;
  const size_t firstWord = pos;

  // Calls visit(index, word) for each word from firstWord on; false if a word is too long.
  const auto eachWord = [&](auto&& visit) {
    size_t at = firstWord;
    size_t index = 0;
    while (at < n) {
      const size_t start = at;
      while (at < n && line[at] != ' ') ++at;
      const size_t len = at - start;
      if (len >= MAX_WORD_BUF) return false;
      line.copy(wordBuf, len, start);
      wordBuf[len] = '\0';
      if (!visit(index++, wordBuf)) return false;
      while (at < n && line[at] == ' ') ++at;
    }
    return true;
  };

  int totalWordWidth = 0;
  const bool measured = eachWord([&](const size_t index, const char* word) {
    if (index >= MAX_WORDS) return false;
    const int w = measureWord(word);
    if (w < 0 || w > UINT16_MAX) return false;
    widths[index] = static_cast<uint16_t>(w);
    totalWordWidth += w;
    count = index + 1;
    return true;
  });
  if (!measured || count < 2) return false;

  const int leadingOffset = static_cast<int>(firstWord) * spaceWidth;
  const int naturalWidth = leadingOffset + totalWordWidth + static_cast<int>(count - 1) * spaceWidth;
  const int spareSpace = contentWidth - naturalWidth;
  if (spareSpace <= 0) return false;

  JustifySpacing spacing(spareSpace, count - 1);
  int x = leadingOffset;
  eachWord([&](const size_t index, const char* word) {
    emitWord(x, word);
    x += widths[index];
    if (index + 1 < count) x += spaceWidth + spacing.nextExtra();
    return true;
  });
  return true;
}

}  // namespace txt_justify
