#pragma once

#include <cstddef>
#include <cstdint>

// Pure, Arduino-free logic for drawing a saved quote's underline back onto the
// page. Split out of EpubReaderActivity the same way ParagraphNumberLayout.h is,
// so the matcher and the baseline arithmetic can be host tested.
//
// The position of a quote is recovered, not stored: the page's word tokens are
// scanned for the run whose text equals the saved quote. That is immune to
// relayout (font size, margins, orientation) because it compares content, not
// coordinates, and it is self-validating — either the text is on this page or
// nothing is drawn.
//
// Comparison ignores every space and every hyphen on both sides. Hyphenation
// inserts a token when a word is broken across lines (ParsedText splits
// "example" into "exam-" + "ple"), so a quote saved with one hyphenation
// setting must still match the same passage laid out with another.
namespace quote_underline {

// A quote longer than this is never underlined: the scratch buffer that holds it
// during matching is bounded, and very long selections are rare.
inline constexpr size_t MAX_MATCH_BYTES = 768;

// Per-page caps. Every one of these is a hard refusal, not a resize.
inline constexpr size_t MAX_QUOTES_PER_PAGE = 4;
inline constexpr size_t MAX_SEGMENTS_PER_PAGE = 32;

// Bytes to skip at `p` when it starts an ignorable sequence (whitespace, an
// ASCII hyphen, or a UTF-8 soft hyphen U+00AD = 0xC2 0xAD). 0 = significant.
inline size_t ignorableLen(const char* p) {
  const auto c = static_cast<unsigned char>(p[0]);
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-') return 1;
  if (c == 0xC2 && static_cast<unsigned char>(p[1]) == 0xAD) return 2;
  return 0;
}

// First significant byte of a NUL-terminated token, or '\0' when it has none.
inline char firstSignificant(const char* s) {
  if (!s) return '\0';
  size_t i = 0;
  while (s[i]) {
    const size_t skip = ignorableLen(s + i);
    if (skip == 0) return s[i];
    i += skip;
  }
  return '\0';
}

// True when `quote`, read from `qStart`, holds nothing but ignorable bytes.
inline bool quoteExhausted(const char* quote, size_t qStart) {
  size_t i = qStart;
  while (quote[i]) {
    const size_t skip = ignorableLen(quote + i);
    if (skip == 0) return false;
    i += skip;
  }
  return true;
}

// Does the run starting at word `start` spell out `quote`? On success `outLast`
// is the last word of the run. A quote that runs out mid-word fails: the
// underline covers whole words or nothing.
inline bool matchRunAt(const char* const* words, const size_t wordCount, const char* quote, const size_t start,
                       size_t& outLast) {
  size_t qi = 0;
  for (size_t w = start; w < wordCount; w++) {
    const char* p = words[w];
    if (!p) return false;
    size_t i = 0;
    while (p[i]) {
      const size_t skipWord = ignorableLen(p + i);
      if (skipWord != 0) {
        i += skipWord;
        continue;
      }
      size_t skipQuote;
      while (quote[qi] && (skipQuote = ignorableLen(quote + qi)) != 0) qi += skipQuote;
      if (quote[qi] != p[i]) return false;
      qi++;
      i++;
    }
    if (quoteExhausted(quote, qi)) {
      outLast = w;
      return true;
    }
  }
  return false;  // page ran out before the quote did
}

// Scan `words` for `quote`, preferring the occurrence nearest `startHint` (the
// index the quote was grabbed at, so a passage repeated inside one paragraph
// resolves to the one the user actually picked). Search wraps to cover the page.
// Returns false and leaves the outputs alone when there is no match — the caller
// then draws nothing, which is the safe outcome.
inline bool findQuoteRun(const char* const* words, const size_t wordCount, const char* quote, const size_t startHint,
                         size_t& outFirst, size_t& outLast) {
  if (!words || !quote || wordCount == 0) return false;
  const char firstChar = firstSignificant(quote);
  if (firstChar == '\0') return false;  // empty or punctuation-only quote

  const size_t hint = (startHint < wordCount) ? startHint : 0;
  for (size_t offset = 0; offset < wordCount; offset++) {
    const size_t start = (hint + offset) % wordCount;
    if (firstSignificant(words[start]) != firstChar) continue;  // cheap reject
    size_t last = 0;
    if (matchRunAt(words, wordCount, quote, start, last)) {
      outFirst = start;
      outLast = last;
      return true;
    }
  }
  return false;
}

// A hairline at the smallest reading size, two pixels once the text is big
// enough that one pixel would look like dirt on the panel.
inline int underlineThickness(const int lineHeight) { return (lineHeight >= 28) ? 2 : 1; }

// Baseline for the underline: just under the glyph bodies, and never inside the
// next line's box. `lineTop` is the line's y on screen, `ascender` the font's
// ascent above the baseline.
inline int underlineY(const int lineTop, const int ascender, const int lineHeight, const int thickness) {
  const int gap = (lineHeight / 12 > 1) ? lineHeight / 12 : 1;
  const int wanted = lineTop + ascender + gap;
  const int maxY = lineTop + lineHeight - thickness;
  if (maxY <= lineTop) return lineTop;
  return (wanted > maxY) ? maxY : wanted;
}

}  // namespace quote_underline
