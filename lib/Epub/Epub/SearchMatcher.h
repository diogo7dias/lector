#pragma once
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <string>

// Finds a query in a stream of visible codepoints, the way the reader counts them, and
// hands back each hit's chapter offset with a short snippet around it.
//
// Matching folds ASCII and Latin-1 case, curly quotes to straight ones, and collapses any
// run of whitespace to one space, so "it's" finds "It’s" across a line break. Other scripts
// match exactly. Header-only and allocation-free while feeding, so it is host-tested
// (test/search_matcher) and cheap on the device.
class SearchMatcher {
 public:
  static constexpr int MAX_QUERY = 48;       // codepoints
  static constexpr int SNIPPET_BEFORE = 24;  // codepoints of context before a hit
  static constexpr int SNIPPET_AFTER = 40;   // and after its start

  // False for a query with nothing but whitespace, or one longer than MAX_QUERY.
  bool setQuery(const char* utf8) {
    qLen = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(utf8);
    while (*p) {
      const uint32_t cp = fold(utf8NextCodepoint(&p));
      if (cp == ' ' && (qLen == 0 || query[qLen - 1] == ' ')) continue;
      if (qLen == MAX_QUERY) return false;
      query[qLen++] = cp;
    }
    while (qLen > 0 && query[qLen - 1] == ' ') --qLen;
    return qLen > 0;
  }

  // Forget the window: the next chapter starts with no text before it.
  void reset() {
    winCount = 0;
    ctxCount = 0;
    pending = false;
  }

  // Feed one visible codepoint and its chapter offset. onHit(offset, snippet) runs when a
  // hit's snippet is complete; call finish() at the chapter's end for the last one.
  template <typename OnHit>
  void feed(const uint32_t rawCp, const uint32_t offset, OnHit&& onHit) {
    const uint32_t cp = fold(rawCp);
    const bool space = cp == ' ';
    if (space && (winCount == 0 || win[(winHead + winCount - 1) % MAX_QUERY] == ' ')) return;

    pushContext(space ? ' ' : rawCp);
    if (pending && ++pendingAfter >= SNIPPET_AFTER) emit(onHit);

    // The window holds the last qLen codepoints: winHead is the oldest.
    win[(winHead + winCount) % MAX_QUERY] = cp;
    winOff[(winHead + winCount) % MAX_QUERY] = offset;
    if (winCount < qLen) {
      ++winCount;
    } else {
      winHead = (winHead + 1) % MAX_QUERY;
    }
    if (winCount < qLen) return;
    for (int i = 0; i < qLen; ++i) {
      if (win[(winHead + i) % MAX_QUERY] != query[i]) return;
    }
    if (pending) emit(onHit);  // a hit inside the last one's tail: close that snippet early
    pending = true;
    pendingOffset = winOff[winHead];
    pendingAfter = qLen;
    // Context codepoints before the hit: everything held except the query itself.
    pendingStart = ctxCount > qLen + SNIPPET_BEFORE ? ctxCount - qLen - SNIPPET_BEFORE : 0;
  }

  template <typename OnHit>
  void finish(OnHit&& onHit) {
    if (pending) emit(onHit);
  }

 private:
  static constexpr int CTX = SNIPPET_BEFORE + MAX_QUERY + SNIPPET_AFTER;

  static uint32_t fold(const uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;  // Latin-1 capitals
    switch (cp) {
      case '\t':
      case '\n':
      case '\r':
      case 0xA0:    // no-break space
      case 0x2009:  // thin space
      case 0x202F:  // narrow no-break space
        return ' ';
      case 0x2018:
      case 0x2019:
        return '\'';
      case 0x201C:
      case 0x201D:
        return '"';
      default:
        return cp;
    }
  }

  // A sliding record of the last CTX codepoints as shown (whitespace already collapsed).
  // ctxCount only grows; ctx[] holds the newest CTX of them.
  void pushContext(const uint32_t cp) {
    ctx[ctxCount % CTX] = cp;
    ++ctxCount;
  }

  template <typename OnHit>
  void emit(OnHit&& onHit) {
    pending = false;
    const uint32_t from = std::max(pendingStart, ctxCount > CTX ? ctxCount - CTX : 0u);
    std::string snippet;
    snippet.reserve((ctxCount - from) + 8);
    if (from > 0) snippet += "…";
    for (uint32_t i = from; i < ctxCount; ++i) utf8AppendCodepoint(ctx[i % CTX], snippet);
    if (pendingAfter >= SNIPPET_AFTER) snippet += "…";
    onHit(pendingOffset, snippet);
  }

  uint32_t query[MAX_QUERY] = {};
  int qLen = 0;
  uint32_t win[MAX_QUERY] = {};
  uint32_t winOff[MAX_QUERY] = {};
  int winHead = 0;
  int winCount = 0;
  uint32_t ctx[CTX] = {};
  uint32_t ctxCount = 0;
  bool pending = false;
  uint32_t pendingOffset = 0;
  int pendingAfter = 0;
  uint32_t pendingStart = 0;
};
