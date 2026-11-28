#pragma once

#include <Epub/JustifySpacing.h>

#include <cstddef>
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
  constexpr size_t MAX_WORDS = 48;
  constexpr size_t MAX_WORD_BUF = 128;

  struct Span {
    size_t start;
    size_t len;
    int width;
  };
  Span spans[MAX_WORDS];
  size_t count = 0;

  const size_t n = line.size();
  size_t pos = 0;
  int leadingSpaceCount = 0;
  while (pos < n && line[pos] == ' ') {
    ++leadingSpaceCount;
    ++pos;
  }

  int totalWordWidth = 0;
  while (pos < n) {
    const size_t start = pos;
    while (pos < n && line[pos] != ' ') ++pos;
    const size_t len = pos - start;
    if (len > 0) {
      if (count >= MAX_WORDS || len >= MAX_WORD_BUF) return false;
      char wordBuf[MAX_WORD_BUF];
      line.copy(wordBuf, len, start);
      wordBuf[len] = '\0';
      const int w = measureWord(wordBuf);
      spans[count++] = {start, len, w};
      totalWordWidth += w;
    }
    while (pos < n && line[pos] == ' ') ++pos;
  }

  if (count < 2) return false;

  const int leadingOffset = leadingSpaceCount * spaceWidth;
  const int naturalWidth = leadingOffset + totalWordWidth + static_cast<int>(count - 1) * spaceWidth;
  const int spareSpace = contentWidth - naturalWidth;
  if (spareSpace <= 0) return false;

  JustifySpacing spacing(spareSpace, count - 1);
  int x = leadingOffset;
  for (size_t i = 0; i < count; ++i) {
    char wordBuf[MAX_WORD_BUF];
    line.copy(wordBuf, spans[i].len, spans[i].start);
    wordBuf[spans[i].len] = '\0';
    emitWord(x, wordBuf);
    x += spans[i].width;
    if (i + 1 < count) {
      x += spaceWidth + spacing.nextExtra();
    }
  }
  return true;
}

}  // namespace txt_justify
