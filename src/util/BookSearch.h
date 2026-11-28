#pragma once

// Streaming in-book search for EPUB (HTML/XHTML) and TXT.
//
// Uses a fixed 2 KB ring buffer with zero dynamic growth during scanning.
// Heap-allocate HtmlStreamScanner / TxtStreamScanner via makeUniqueNoThrow
// (never on the ESP32-C3 stack). Free of Arduino/HalStorage so host-tested.

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace book_search {

inline constexpr size_t RING_SIZE = 2048;
inline constexpr size_t SNIPPET_BYTES = 48;
inline constexpr size_t MAX_QUERY_BYTES = 64;

#if defined(BOARD_HAS_PSRAM) || !defined(ARDUINO)
inline constexpr size_t DEFAULT_MAX_HITS = 128;
#else
inline constexpr size_t DEFAULT_MAX_HITS = 32;
#endif

struct SearchHit {
  int spineIndex = 0;   // EPUB spine index; 0 for TXT
  uint32_t offset = 0;  // EPUB visibleTextOffset (codepoints); TXT byte offset
  char snippet[SNIPPET_BYTES] = {};
};

inline char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool isAsciiSpace(const unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline bool isUtf8Continuation(const unsigned char c) { return (c & 0xC0) == 0x80; }

// Normalizes a search query: trims outer ASCII whitespace, collapses inner ASCII
// whitespace runs to a single space, lowercases ASCII, and caps at MAX_QUERY_BYTES.
// Returns normalized byte length (0 if empty).
inline size_t normalizeQuery(std::string_view raw, char (&out)[MAX_QUERY_BYTES + 1]) {
  size_t len = 0;
  bool lastSpace = true;  // true initially to skip leading whitespace
  for (const char ch : raw) {
    const unsigned char u = static_cast<unsigned char>(ch);
    if (isAsciiSpace(u)) {
      if (!lastSpace && len < MAX_QUERY_BYTES) {
        out[len++] = ' ';
        lastSpace = true;
      }
      continue;
    }
    if (len >= MAX_QUERY_BYTES) break;
    out[len++] = lowerAscii(ch);
    lastSpace = false;
  }
  while (len > 0 && out[len - 1] == ' ') --len;
  // Never cut a multi-byte UTF-8 sequence in half if capped at MAX_QUERY_BYTES.
  if (len == MAX_QUERY_BYTES && raw.size() > MAX_QUERY_BYTES) {
    size_t cut = len;
    while (cut > 0 && isUtf8Continuation(static_cast<unsigned char>(out[cut - 1]))) --cut;
    if (cut > 0) {
      const unsigned char lead = static_cast<unsigned char>(out[cut - 1]);
      size_t expected = 1;
      if ((lead & 0xE0) == 0xC0)
        expected = 2;
      else if ((lead & 0xF0) == 0xE0)
        expected = 3;
      else if ((lead & 0xF8) == 0xF0)
        expected = 4;
      if (len - (cut - 1) < expected) len = cut - 1;
    }
  }
  out[len] = '\0';
  return len;
}

// Encodes a Unicode scalar value to UTF-8. Returns 0 if out of range / surrogate.
inline size_t encodeUtf8(const uint32_t cp, char out[4]) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

// Fixed-size 2 KB ring matcher shared by HTML and TXT scanners.
class RingMatcher {
 public:
  RingMatcher(std::string_view query, const size_t maxHits = DEFAULT_MAX_HITS) : maxHits_(maxHits) {
    queryLen_ = normalizeQuery(query, queryLower_);
    hits_.reserve(std::min<size_t>(maxHits_, 32));
  }

  bool valid() const { return queryLen_ > 0 && maxHits_ > 0; }
  bool isFull() const { return hits_.size() + pendingCount_ >= maxHits_; }
  bool truncated() const { return truncated_; }
  const std::vector<SearchHit>& hits() const { return hits_; }
  std::vector<SearchHit> takeHits() {
    flushPending();
    return std::move(hits_);
  }

  void beginStream(const int spineIndex) {
    flushPending();
    spineIndex_ = spineIndex;
    totalEmitted_ = 0;
    lastEmitted_ = 0;
    lastHitEnd_ = 0;
  }

  void finishStream() { flushPending(); }

  // Emits a whitespace separator between block elements without claiming a match inside it.
  void emitVirtualSpace(const uint32_t offset) {
    if (totalEmitted_ == 0 || lastEmitted_ == ' ') return;
    pushByte(' ', offset);
  }

  // Feeds one visible byte at `offset` (EPUB visibleTextOffset or TXT byte offset).
  void feedByte(const char rawByte, const uint32_t offset) {
    if (!valid()) return;
    const unsigned char u = static_cast<unsigned char>(rawByte);
    if (isAsciiSpace(u)) {
      if (totalEmitted_ == 0 || lastEmitted_ == ' ') return;
      pushByte(' ', offset);
      return;
    }
    pushByte(rawByte, offset);
  }

 private:
  static constexpr size_t POST_CONTEXT = 22;
  static constexpr size_t MAX_PENDING = 4;

  struct PendingHit {
    uint64_t matchStart = 0;
    uint32_t offset = 0;
    int spineIndex = 0;
  };

  void pushByte(const char b, const uint32_t offset) {
    const uint64_t idx = totalEmitted_;
    ring_[idx % RING_SIZE] = b;
    recentOffsets_[idx % MAX_QUERY_BYTES] = offset;
    totalEmitted_ = idx + 1;
    lastEmitted_ = b;

    // Finalize any queued hit that now has enough post-match context (or is about to age out).
    drainReadyPending();

    if (totalEmitted_ < queryLen_) return;
    if (lowerAscii(b) != queryLower_[queryLen_ - 1]) return;

    const uint64_t matchStart = totalEmitted_ - queryLen_;
    if (matchStart < lastHitEnd_) return;

    for (size_t i = 0; i < queryLen_; ++i) {
      if (lowerAscii(ring_[(matchStart + i) % RING_SIZE]) != queryLower_[i]) return;
    }

    if (hits_.size() + pendingCount_ >= maxHits_) {
      truncated_ = true;
      return;
    }
    if (pendingCount_ == MAX_PENDING) {
      finalizeOne(pending_[0]);
      for (size_t i = 1; i < pendingCount_; ++i) pending_[i - 1] = pending_[i];
      --pendingCount_;
    }
    pending_[pendingCount_++] = PendingHit{matchStart, recentOffsets_[matchStart % MAX_QUERY_BYTES], spineIndex_};
    lastHitEnd_ = totalEmitted_;
  }

  void drainReadyPending() {
    size_t writeIdx = 0;
    for (size_t i = 0; i < pendingCount_; ++i) {
      const PendingHit& p = pending_[i];
      const bool enoughPost = totalEmitted_ >= p.matchStart + queryLen_ + POST_CONTEXT;
      const bool nearEvict = totalEmitted_ >= RING_SIZE && p.matchStart <= totalEmitted_ - RING_SIZE + 32;
      if (enoughPost || nearEvict) {
        finalizeOne(p);
      } else {
        pending_[writeIdx++] = p;
      }
    }
    pendingCount_ = writeIdx;
  }

  void flushPending() {
    for (size_t i = 0; i < pendingCount_; ++i) finalizeOne(pending_[i]);
    pendingCount_ = 0;
  }

  void finalizeOne(const PendingHit& p) {
    if (hits_.size() >= maxHits_) {
      truncated_ = true;
      return;
    }
    SearchHit hit;
    hit.spineIndex = p.spineIndex;
    hit.offset = p.offset;

    const uint64_t minAvail = totalEmitted_ > RING_SIZE ? totalEmitted_ - RING_SIZE : 0;
    const size_t maxText = SNIPPET_BYTES - 1;
    const size_t preWant = queryLen_ + 6 < maxText ? std::min<size_t>(16, (maxText - queryLen_) / 2) : 0;
    uint64_t start = p.matchStart > preWant ? std::max(minAvail, p.matchStart - preWant) : minAvail;
    while (start < totalEmitted_ && isUtf8Continuation(static_cast<unsigned char>(ring_[start % RING_SIZE]))) ++start;
    while (start < p.matchStart && ring_[start % RING_SIZE] == ' ') ++start;

    size_t outLen = 0;
    uint64_t cur = start;
    while (cur < totalEmitted_ && outLen < maxText) {
      const unsigned char c = static_cast<unsigned char>(ring_[cur % RING_SIZE]);
      size_t seqLen = 1;
      if ((c & 0xE0) == 0xC0)
        seqLen = 2;
      else if ((c & 0xF0) == 0xE0)
        seqLen = 3;
      else if ((c & 0xF8) == 0xF0)
        seqLen = 4;
      if (cur + seqLen > totalEmitted_ || outLen + seqLen > maxText) break;
      for (size_t k = 0; k < seqLen; ++k) {
        hit.snippet[outLen++] = ring_[(cur + k) % RING_SIZE];
      }
      cur += seqLen;
    }
    while (outLen > 0 && hit.snippet[outLen - 1] == ' ') --outLen;
    hit.snippet[outLen] = '\0';
    hits_.push_back(hit);
  }

  char ring_[RING_SIZE] = {};
  uint32_t recentOffsets_[MAX_QUERY_BYTES] = {};
  char queryLower_[MAX_QUERY_BYTES + 1] = {};
  size_t queryLen_ = 0;
  size_t maxHits_ = DEFAULT_MAX_HITS;
  int spineIndex_ = 0;
  uint64_t totalEmitted_ = 0;
  char lastEmitted_ = 0;
  uint64_t lastHitEnd_ = 0;
  std::array<PendingHit, MAX_PENDING> pending_{};
  size_t pendingCount_ = 0;
  bool truncated_ = false;
  std::vector<SearchHit> hits_;
};

// Streaming HTML/XHTML scanner that strips tags, skips non-visible elements
// (head/style/script/title/rp), decodes HTML entities, and tracks visibleTextOffset
// identically to ChapterHtmlSlimParser.
class HtmlStreamScanner {
 public:
  HtmlStreamScanner(std::string_view query, const size_t maxHits = DEFAULT_MAX_HITS) : matcher_(query, maxHits) {}

  bool valid() const { return matcher_.valid(); }
  bool isFull() const { return matcher_.isFull(); }
  bool truncated() const { return matcher_.truncated(); }
  uint32_t visibleTextOffset() const { return visibleTextOffset_; }
  const std::vector<SearchHit>& hits() const { return matcher_.hits(); }
  std::vector<SearchHit> takeHits() { return matcher_.takeHits(); }

  void beginChapter(const int spineIndex) {
    matcher_.beginStream(spineIndex);
    visibleTextOffset_ = 0;
    insideBody_ = false;
    nonVisibleDepth_ = 0;
    state_ = State::Text;
    tagLen_ = 0;
    closingTag_ = false;
    selfClosing_ = false;
    attrQuote_ = 0;
    entityLen_ = 0;
    commentDashes_ = 0;
  }

  void finishChapter() {
    if (state_ == State::Entity) flushEntityLiteral();
    matcher_.finishStream();
  }

  void feed(const char* data, const size_t len) {
    if (!matcher_.valid() || matcher_.isFull()) return;
    for (size_t i = 0; i < len; ++i) {
      if (matcher_.isFull()) break;
      processByte(data[i]);
    }
  }

 private:
  enum class State : uint8_t { Text, TagStart, TagName, TagAttrs, Comment, Entity };

  static bool isBlockSeparatorTag(const std::string_view name) {
    return VisibleTextUtils::equalsTag(name, "p") || VisibleTextUtils::equalsTag(name, "div") ||
           VisibleTextUtils::equalsTag(name, "br") || VisibleTextUtils::equalsTag(name, "li") ||
           VisibleTextUtils::equalsTag(name, "tr") || VisibleTextUtils::equalsTag(name, "td") ||
           VisibleTextUtils::equalsTag(name, "th") || VisibleTextUtils::equalsTag(name, "blockquote") ||
           VisibleTextUtils::equalsTag(name, "hr") || VisibleTextUtils::equalsTag(name, "section") ||
           VisibleTextUtils::equalsTag(name, "article") ||
           (name.size() == 2 && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6');
  }

  void emitVisibleBytes(const char* s, const size_t len) {
    if (!insideBody_ || nonVisibleDepth_ > 0) return;
    for (size_t i = 0; i < len; ++i) {
      const unsigned char u = static_cast<unsigned char>(s[i]);
      const uint32_t byteOffset = visibleTextOffset_;
      if (!isUtf8Continuation(u)) ++visibleTextOffset_;
      matcher_.feedByte(s[i], byteOffset);
    }
  }

  void onTagFinished() {
    const std::string_view name(tagBuf_, tagLen_);
    if (name.empty()) return;

    if (VisibleTextUtils::equalsTag(name, "body")) {
      insideBody_ = !closingTag_;
      if (closingTag_) nonVisibleDepth_ = 0;
      return;
    }
    if (!insideBody_) return;

    if (closingTag_) {
      if (nonVisibleDepth_ > 0) {
        --nonVisibleDepth_;
        return;
      }
      if (isBlockSeparatorTag(name)) matcher_.emitVirtualSpace(visibleTextOffset_);
      return;
    }

    if (!selfClosing_ && (nonVisibleDepth_ > 0 || VisibleTextUtils::isNonVisibleElement(name))) {
      ++nonVisibleDepth_;
      return;
    }
    if (nonVisibleDepth_ == 0 && isBlockSeparatorTag(name)) {
      matcher_.emitVirtualSpace(visibleTextOffset_);
    }
  }

  void flushEntityLiteral() {
    emitVisibleBytes(entityBuf_, entityLen_);
    entityLen_ = 0;
    state_ = State::Text;
  }

  void finishEntity() {
    entityBuf_[entityLen_] = '\0';
    if (entityLen_ >= 2 && entityBuf_[0] == '&') {
      if (entityLen_ >= 3 && entityBuf_[1] == '#') {
        uint32_t cp = 0;
        bool ok = false;
        if (entityBuf_[2] == 'x' || entityBuf_[2] == 'X') {
          for (size_t i = 3; i < entityLen_; ++i) {
            const char ch = entityBuf_[i];
            if (ch >= '0' && ch <= '9') {
              cp = (cp << 4) | static_cast<uint32_t>(ch - '0');
              ok = true;
            } else if (ch >= 'a' && ch <= 'f') {
              cp = (cp << 4) | static_cast<uint32_t>(10 + ch - 'a');
              ok = true;
            } else if (ch >= 'A' && ch <= 'F') {
              cp = (cp << 4) | static_cast<uint32_t>(10 + ch - 'A');
              ok = true;
            } else {
              ok = false;
              break;
            }
          }
        } else {
          for (size_t i = 2; i < entityLen_; ++i) {
            if (entityBuf_[i] >= '0' && entityBuf_[i] <= '9') {
              cp = cp * 10 + static_cast<uint32_t>(entityBuf_[i] - '0');
              ok = true;
            } else {
              ok = false;
              break;
            }
          }
        }
        char utf8[4];
        const size_t n = ok ? encodeUtf8(cp, utf8) : 0;
        if (n > 0) {
          emitVisibleBytes(utf8, n);
          entityLen_ = 0;
          state_ = State::Text;
          return;
        }
      } else if (entityLen_ < sizeof(entityBuf_) - 1) {
        // lookupHtmlEntity expects the full "&name;" token.
        entityBuf_[entityLen_] = ';';
        const char* resolved = lookupHtmlEntity(entityBuf_, entityLen_ + 1);
        if (resolved) {
          emitVisibleBytes(resolved, std::strlen(resolved));
          entityLen_ = 0;
          state_ = State::Text;
          return;
        }
      }
    }
    flushEntityLiteral();
    emitVisibleBytes(";", 1);
  }

  void processByte(const char c) {
    switch (state_) {
      case State::Text:
        if (c == '<') {
          state_ = State::TagStart;
          tagLen_ = 0;
          closingTag_ = false;
          selfClosing_ = false;
          attrQuote_ = 0;
          commentDashes_ = 0;
        } else if (c == '&') {
          state_ = State::Entity;
          entityBuf_[0] = '&';
          entityLen_ = 1;
        } else {
          emitVisibleBytes(&c, 1);
        }
        break;

      case State::TagStart:
        if (c == '/') {
          closingTag_ = true;
          state_ = State::TagName;
        } else if (c == '!') {
          state_ = State::Comment;
          commentDashes_ = 0;
        } else if (c == '?') {
          state_ = State::TagAttrs;
        } else if (c == '>') {
          state_ = State::Text;
        } else if (!isAsciiSpace(static_cast<unsigned char>(c))) {
          tagBuf_[tagLen_++] = c;
          state_ = State::TagName;
        }
        break;

      case State::TagName:
        if (c == '>') {
          onTagFinished();
          state_ = State::Text;
        } else if (c == '/') {
          selfClosing_ = true;
          state_ = State::TagAttrs;
        } else if (isAsciiSpace(static_cast<unsigned char>(c))) {
          state_ = State::TagAttrs;
        } else if (tagLen_ < sizeof(tagBuf_) - 1) {
          tagBuf_[tagLen_++] = c;
        }
        break;

      case State::TagAttrs:
        if (attrQuote_ != 0) {
          if (c == attrQuote_) attrQuote_ = 0;
        } else if (c == '"' || c == '\'') {
          attrQuote_ = c;
        } else if (c == '/') {
          selfClosing_ = true;
        } else if (c == '>') {
          onTagFinished();
          state_ = State::Text;
        } else if (!isAsciiSpace(static_cast<unsigned char>(c))) {
          selfClosing_ = false;
        }
        break;

      case State::Comment:
        // Handles both <!-- ... --> and <!DOCTYPE ...> / <![CDATA[...]]>.
        if (tagLen_ == 0 && c == '-') {
          if (++commentDashes_ == 2) {
            tagLen_ = 1;  // mark "inside <!-- comment -->"
            commentDashes_ = 0;
          }
        } else if (tagLen_ == 1) {
          if (c == '-') {
            if (commentDashes_ < 2) ++commentDashes_;
          } else if (c == '>' && commentDashes_ >= 2) {
            state_ = State::Text;
          } else {
            commentDashes_ = 0;
          }
        } else {
          if (c == '>') state_ = State::Text;
        }
        break;

      case State::Entity:
        if (c == ';') {
          finishEntity();
        } else if (c == '<') {
          flushEntityLiteral();
          processByte('<');
        } else if (c == '&') {
          flushEntityLiteral();
          state_ = State::Entity;
          entityBuf_[0] = '&';
          entityLen_ = 1;
        } else if (entityLen_ < sizeof(entityBuf_) - 1 &&
                   (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '#')) {
          entityBuf_[entityLen_++] = c;
        } else {
          flushEntityLiteral();
          emitVisibleBytes(&c, 1);
        }
        break;
    }
  }

  RingMatcher matcher_;
  uint32_t visibleTextOffset_ = 0;
  bool insideBody_ = false;
  uint8_t nonVisibleDepth_ = 0;
  State state_ = State::Text;
  char tagBuf_[24] = {};
  uint8_t tagLen_ = 0;
  bool closingTag_ = false;
  bool selfClosing_ = false;
  char attrQuote_ = 0;
  char entityBuf_[16] = {};
  uint8_t entityLen_ = 0;
  uint8_t commentDashes_ = 0;
};

// Streaming plain-text scanner (TXT books). Tracks exact byte offset in file.
class TxtStreamScanner {
 public:
  TxtStreamScanner(std::string_view query, const size_t maxHits = DEFAULT_MAX_HITS) : matcher_(query, maxHits) {
    matcher_.beginStream(0);
  }

  bool valid() const { return matcher_.valid(); }
  bool isFull() const { return matcher_.isFull(); }
  bool truncated() const { return matcher_.truncated(); }
  const std::vector<SearchHit>& hits() const { return matcher_.hits(); }
  std::vector<SearchHit> takeHits() { return matcher_.takeHits(); }

  void feed(const char* data, const size_t len) {
    if (!matcher_.valid() || matcher_.isFull()) return;
    for (size_t i = 0; i < len; ++i) {
      if (matcher_.isFull()) break;
      matcher_.feedByte(data[i], byteOffset_);
      ++byteOffset_;
    }
  }

  void finish() { matcher_.finishStream(); }

 private:
  RingMatcher matcher_;
  uint32_t byteOffset_ = 0;
};

}  // namespace book_search
