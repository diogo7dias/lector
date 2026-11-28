#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace footnote_extract {

constexpr size_t DEFAULT_MAX_BYTES = 2048;

// Splits an internal EPUB href ("notes.xhtml#fn1", "#fn1", "notes.xhtml") into
// file part and URL-decoded fragment id. Returns true when a non-empty fragment
// is present.
bool splitHref(std::string_view href, std::string& outFile, std::string& outFragment);

// True when `tagName` is <aside> with an `id`/`xml:id` and either epub:type
// containing footnote/endnote/rearnote or role containing doc-footnote/doc-endnote.
// When non-null, `outId` receives the matched id attribute value.
bool isFootnoteAsideElement(const char* tagName, const char* const* atts, const char** outId = nullptr);

// Streaming HTML/XHTML extractor for a single target `#id` element.
// Designed for ZipFile::readFileToStream(..., allowEarlyStop = true): zero
// full-chapter buffering, <256 bytes stack usage, UTF-8-safe byte cap.
class FootnoteTextExtractor {
 public:
  explicit FootnoteTextExtractor(std::string targetId, size_t maxBytes = DEFAULT_MAX_BYTES);

  // Feed the next raw XHTML chunk. Returns false once the target element closes
  // or hits `maxBytes` so the caller can stop unzipping early.
  bool feed(const char* data, size_t len);

  bool found() const { return found_; }
  bool truncated() const { return truncated_; }
  bool isFootnoteAside() const { return isFootnoteAside_; }
  const std::string& text() const { return text_; }

 private:
  enum class State : uint8_t {
    Text,
    TagOpen,
    TagName,
    TagAttrWait,
    TagAttrName,
    TagAttrAfterName,
    TagAttrValWait,
    TagAttrValQuoted,
    TagAttrValUnquoted,
    CommentOrDecl,
    Comment,
    Entity,
  };

  void resetTagState();
  void finishStartTag();
  void finishEndTag();
  void commitAttribute();
  void appendTextByte(char c);
  void appendUtf8(const char* s, size_t len);
  void flushEntity();
  void noteBlockBreak();

  std::string targetId_;
  size_t maxBytes_ = DEFAULT_MAX_BYTES;
  std::string text_;

  State state_ = State::Text;
  bool found_ = false;
  bool capturing_ = false;
  bool done_ = false;
  bool truncated_ = false;
  bool isFootnoteAside_ = false;
  bool pendingSpace_ = false;
  bool pendingNewline_ = false;

  int tagDepth_ = 0;
  int captureEndDepth_ = -1;
  int enclosingBlockDepth_ = -1;
  int skipRawDepth_ = -1;
  static constexpr int MAX_BLOCK_STACK = 32;
  int blockDepthStack_[MAX_BLOCK_STACK] = {};
  int blockStackLen_ = 0;

  bool tagIsClosing_ = false;
  bool tagIsSelfClosing_ = false;
  bool tagHasFootnoteRole_ = false;
  char attrQuote_ = 0;
  char commentDashes_ = 0;
  uint8_t commentEndDashes_ = 0;

  char tagName_[24] = {};
  uint8_t tagNameLen_ = 0;
  char attrName_[24] = {};
  uint8_t attrNameLen_ = 0;
  char attrVal_[128] = {};
  uint8_t attrValLen_ = 0;
  char tagId_[128] = {};
  uint8_t tagIdLen_ = 0;
  char entityBuf_[16] = {};
  uint8_t entityLen_ = 0;
  char utf8Buf_[4] = {};
  uint8_t utf8Len_ = 0;
  uint8_t utf8Need_ = 0;
};

}  // namespace footnote_extract
