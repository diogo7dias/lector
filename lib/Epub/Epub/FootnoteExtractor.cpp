#include "FootnoteExtractor.h"

#include <cstring>

#include "htmlEntities.h"

namespace footnote_extract {

namespace {

char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool asciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (asciiLower(a[i]) != asciiLower(b[i])) return false;
  }
  return true;
}

bool hasTokenIgnoreCase(std::string_view list, std::string_view token) {
  size_t pos = 0;
  while (pos < list.size()) {
    while (pos < list.size() && asciiSpace(list[pos])) ++pos;
    const size_t start = pos;
    while (pos < list.size() && !asciiSpace(list[pos])) ++pos;
    if (pos > start && equalsIgnoreCase(list.substr(start, pos - start), token)) return true;
  }
  return false;
}

bool isFootnoteTypeValue(std::string_view val) {
  return hasTokenIgnoreCase(val, "footnote") || hasTokenIgnoreCase(val, "endnote") ||
         hasTokenIgnoreCase(val, "rearnote");
}

bool isFootnoteRoleValue(std::string_view val) {
  return hasTokenIgnoreCase(val, "doc-footnote") || hasTokenIgnoreCase(val, "doc-endnote");
}

int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percentDecode(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const int hi = hexDigit(input[i + 1]);
      const int lo = hexDigit(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

bool isVoidTag(std::string_view tag) {
  static constexpr const char* kVoid[] = {"area",  "base", "br",   "col",   "embed",  "hr",    "img",
                                          "input", "link", "meta", "param", "source", "track", "wbr"};
  for (const char* v : kVoid) {
    if (tag == v) return true;
  }
  return false;
}

bool isContainerBlockTag(std::string_view tag) {
  static constexpr const char* kBlocks[] = {"p", "li", "div", "aside", "dd", "blockquote", "section"};
  for (const char* b : kBlocks) {
    if (tag == b) return true;
  }
  return false;
}

bool isBreakTag(std::string_view tag) {
  static constexpr const char* kBreak[] = {"p",  "div", "li", "br", "blockquote", "tr", "dd",
                                           "dt", "h1",  "h2", "h3", "h4",         "h5", "h6"};
  for (const char* b : kBreak) {
    if (tag == b) return true;
  }
  return false;
}

bool isInlineAnchorTag(std::string_view tag) { return tag == "a" || tag == "span" || tag == "sup"; }

bool isRawSkipTag(std::string_view tag) { return tag == "style" || tag == "script" || tag == "svg"; }

size_t encodeUtf8(const uint32_t cp, char out[4]) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  if (cp <= 0x7F) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
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

}  // namespace

bool splitHref(const std::string_view href, std::string& outFile, std::string& outFragment) {
  const size_t hash = href.find('#');
  if (hash == std::string_view::npos) {
    outFile.assign(href);
    outFragment.clear();
    return false;
  }
  outFile.assign(href.substr(0, hash));
  outFragment = percentDecode(href.substr(hash + 1));
  return !outFragment.empty();
}

bool isFootnoteAsideElement(const char* tagName, const char* const* atts, const char** outId) {
  if (outId) *outId = nullptr;
  if (!tagName || !atts || !equalsIgnoreCase(tagName, "aside")) return false;
  const char* id = nullptr;
  bool isFootnote = false;
  for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
    const std::string_view name(atts[i]);
    const std::string_view val(atts[i + 1]);
    if (name == "id" || name == "xml:id") {
      if (!val.empty()) id = atts[i + 1];
    } else if (equalsIgnoreCase(name, "epub:type") && isFootnoteTypeValue(val)) {
      isFootnote = true;
    } else if (equalsIgnoreCase(name, "role") && isFootnoteRoleValue(val)) {
      isFootnote = true;
    }
  }
  if (!isFootnote || !id) return false;
  if (outId) *outId = id;
  return true;
}

FootnoteTextExtractor::FootnoteTextExtractor(std::string targetId, const size_t maxBytes)
    : targetId_(std::move(targetId)), maxBytes_(maxBytes) {}

void FootnoteTextExtractor::resetTagState() {
  tagIsClosing_ = false;
  tagIsSelfClosing_ = false;
  tagHasFootnoteRole_ = false;
  attrQuote_ = 0;
  tagNameLen_ = 0;
  attrNameLen_ = 0;
  attrValLen_ = 0;
  tagIdLen_ = 0;
}

void FootnoteTextExtractor::commitAttribute() {
  if (attrNameLen_ == 0) return;
  const std::string_view name(attrName_, attrNameLen_);
  const std::string_view val(attrVal_, attrValLen_);
  if (name == "id" || name == "xml:id") {
    tagIdLen_ = attrValLen_;
    if (tagIdLen_ > 0) memcpy(tagId_, attrVal_, tagIdLen_);
  } else if (name == "epub:type" && isFootnoteTypeValue(val)) {
    tagHasFootnoteRole_ = true;
  } else if (name == "role" && isFootnoteRoleValue(val)) {
    tagHasFootnoteRole_ = true;
  }
  attrNameLen_ = 0;
  attrValLen_ = 0;
}

void FootnoteTextExtractor::noteBlockBreak() {
  if (!text_.empty()) {
    pendingNewline_ = true;
    pendingSpace_ = false;
  }
}

void FootnoteTextExtractor::appendUtf8(const char* s, const size_t len) {
  if (!capturing_ || done_ || len == 0 || skipRawDepth_ >= 0) return;
  const bool emitNewline = pendingNewline_ && !text_.empty();
  const bool emitSpace = !emitNewline && pendingSpace_ && !text_.empty();
  const size_t prefix = (emitNewline || emitSpace) ? 1 : 0;
  if (text_.size() + prefix + len > maxBytes_) {
    truncated_ = true;
    capturing_ = false;
    done_ = true;
    return;
  }
  if (emitNewline)
    text_.push_back('\n');
  else if (emitSpace)
    text_.push_back(' ');
  pendingNewline_ = false;
  pendingSpace_ = false;
  text_.append(s, len);
}

void FootnoteTextExtractor::appendTextByte(const char c) {
  if (!capturing_ || done_ || skipRawDepth_ >= 0) return;
  if (asciiSpace(c)) {
    utf8Len_ = 0;
    utf8Need_ = 0;
    if (!text_.empty() && !pendingNewline_) pendingSpace_ = true;
    return;
  }
  const unsigned char u = static_cast<unsigned char>(c);
  if (u < 0x80) {
    utf8Len_ = 0;
    utf8Need_ = 0;
    appendUtf8(&c, 1);
    return;
  }
  if ((u & 0xC0) == 0xC0) {
    utf8Buf_[0] = c;
    utf8Len_ = 1;
    if ((u & 0xE0) == 0xC0)
      utf8Need_ = 2;
    else if ((u & 0xF0) == 0xE0)
      utf8Need_ = 3;
    else if ((u & 0xF8) == 0xF0)
      utf8Need_ = 4;
    else
      utf8Need_ = 1;
    return;
  }
  if (utf8Need_ > 0 && utf8Len_ < utf8Need_) {
    utf8Buf_[utf8Len_++] = c;
    if (utf8Len_ == utf8Need_) {
      appendUtf8(utf8Buf_, utf8Need_);
      utf8Len_ = 0;
      utf8Need_ = 0;
    }
  }
}

void FootnoteTextExtractor::flushEntity() {
  if (entityLen_ < 2) {
    entityLen_ = 0;
    return;
  }
  // Numeric character reference: &#NNN; or &#xHHH;
  if (entityBuf_[0] == '&' && entityBuf_[1] == '#' && entityBuf_[entityLen_ - 1] == ';') {
    uint32_t cp = 0;
    size_t i = 2;
    const size_t end = static_cast<size_t>(entityLen_) - 1;
    bool valid = false;
    if (i < end && (entityBuf_[i] == 'x' || entityBuf_[i] == 'X')) {
      ++i;
      for (; i < end; ++i) {
        const int d = hexDigit(entityBuf_[i]);
        if (d < 0) {
          valid = false;
          break;
        }
        cp = (cp << 4) | static_cast<uint32_t>(d);
        if (cp > 0x10FFFF) {
          valid = false;
          break;
        }
        valid = true;
      }
    } else {
      for (; i < end; ++i) {
        if (entityBuf_[i] < '0' || entityBuf_[i] > '9') {
          valid = false;
          break;
        }
        cp = cp * 10 + static_cast<uint32_t>(entityBuf_[i] - '0');
        if (cp > 0x10FFFF) {
          valid = false;
          break;
        }
        valid = true;
      }
    }
    if (valid) {
      char utf8[4];
      const size_t n = encodeUtf8(cp, utf8);
      if (n == 1 && asciiSpace(utf8[0])) {
        appendTextByte(' ');
      } else if (n > 0) {
        appendUtf8(utf8, n);
      }
      entityLen_ = 0;
      return;
    }
  }

  const std::string_view ent(entityBuf_, entityLen_);
  if (ent == "&amp;")
    appendUtf8("&", 1);
  else if (ent == "&lt;")
    appendUtf8("<", 1);
  else if (ent == "&gt;")
    appendUtf8(">", 1);
  else if (ent == "&quot;")
    appendUtf8("\"", 1);
  else if (ent == "&apos;")
    appendUtf8("'", 1);
  else if (const char* mapped = lookupHtmlEntity(entityBuf_, entityLen_)) {
    appendUtf8(mapped, strlen(mapped));
  } else {
    for (uint8_t i = 0; i < entityLen_ && !done_; ++i) appendTextByte(entityBuf_[i]);
  }
  entityLen_ = 0;
}

void FootnoteTextExtractor::finishStartTag() {
  commitAttribute();
  const std::string_view tag(tagName_, tagNameLen_);
  const bool selfClosing = tagIsSelfClosing_ || isVoidTag(tag);
  const int thisDepth = tagDepth_;
  if (!selfClosing) ++tagDepth_;

  if (!found_) {
    const std::string_view id(tagId_, tagIdLen_);
    if (!targetId_.empty() && id == targetId_) {
      found_ = true;
      isFootnoteAside_ = (tag == "aside" && tagHasFootnoteRole_);
      if (selfClosing) {
        // Empty anchor like <a id="fn1"/>: capture the rest of the enclosing block.
        if (blockStackLen_ > 0) {
          capturing_ = true;
          captureEndDepth_ = blockDepthStack_[blockStackLen_ - 1];
        } else {
          done_ = true;
        }
      } else {
        capturing_ = true;
        captureEndDepth_ = thisDepth;
        if (isInlineAnchorTag(tag) && blockStackLen_ > 0) {
          enclosingBlockDepth_ = blockDepthStack_[blockStackLen_ - 1];
        }
      }
      return;
    }
    if (!selfClosing && isContainerBlockTag(tag) && blockStackLen_ < MAX_BLOCK_STACK) {
      blockDepthStack_[blockStackLen_++] = thisDepth;
    }
    return;
  }

  if (capturing_) {
    if (skipRawDepth_ < 0 && !selfClosing && isRawSkipTag(tag)) {
      skipRawDepth_ = thisDepth;
    } else if (skipRawDepth_ < 0 && isBreakTag(tag)) {
      noteBlockBreak();
    }
  }
}

void FootnoteTextExtractor::finishEndTag() {
  const std::string_view tag(tagName_, tagNameLen_);
  if (tagDepth_ > 0) --tagDepth_;
  while (blockStackLen_ > 0 && blockDepthStack_[blockStackLen_ - 1] >= tagDepth_) {
    --blockStackLen_;
  }
  if (!capturing_) return;

  if (skipRawDepth_ >= 0) {
    if (tagDepth_ <= skipRawDepth_) skipRawDepth_ = -1;
    return;
  }
  if (isBreakTag(tag)) noteBlockBreak();

  if (tagDepth_ <= captureEndDepth_) {
    // Inline anchor like <a id="fn1">[1]</a> followed by the note text inside the same <p>/<li>.
    if (enclosingBlockDepth_ >= 0 && text_.size() <= 12) {
      captureEndDepth_ = enclosingBlockDepth_;
      enclosingBlockDepth_ = -1;
      if (!text_.empty() && !pendingNewline_) pendingSpace_ = true;
      return;
    }
    capturing_ = false;
    done_ = true;
  }
}

bool FootnoteTextExtractor::feed(const char* data, const size_t len) {
  if (done_) return false;
  if (!data || len == 0) return true;

  for (size_t i = 0; i < len && !done_; ++i) {
    const char c = data[i];
    switch (state_) {
      case State::Text:
        if (c == '<') {
          utf8Len_ = 0;
          utf8Need_ = 0;
          resetTagState();
          state_ = State::TagOpen;
        } else if (c == '&' && capturing_ && skipRawDepth_ < 0) {
          utf8Len_ = 0;
          utf8Need_ = 0;
          entityBuf_[0] = '&';
          entityLen_ = 1;
          state_ = State::Entity;
        } else {
          appendTextByte(c);
        }
        break;

      case State::Entity:
        if (entityLen_ < sizeof(entityBuf_)) entityBuf_[entityLen_++] = c;
        if (c == ';') {
          flushEntity();
          state_ = State::Text;
        } else if (c == '<' || asciiSpace(c) || entityLen_ >= sizeof(entityBuf_)) {
          // Malformed entity: emit buffered literal bytes and reprocess boundary char.
          const uint8_t emitLen = (c == '<') ? static_cast<uint8_t>(entityLen_ - 1) : entityLen_;
          for (uint8_t k = 0; k < emitLen && !done_; ++k) appendTextByte(entityBuf_[k]);
          entityLen_ = 0;
          if (c == '<') {
            resetTagState();
            state_ = State::TagOpen;
          } else {
            state_ = State::Text;
          }
        }
        break;

      case State::TagOpen:
        if (c == '/') {
          tagIsClosing_ = true;
          state_ = State::TagName;
        } else if (c == '!' || c == '?') {
          commentDashes_ = (c == '!') ? 1 : 0;  // 1 means saw "<!", watching for "--"
          commentEndDashes_ = 0;
          state_ = State::CommentOrDecl;
        } else if (c == '>') {
          state_ = State::Text;
        } else if (!asciiSpace(c)) {
          tagName_[tagNameLen_++] = asciiLower(c);
          state_ = State::TagName;
        }
        break;

      case State::CommentOrDecl:
        if (commentDashes_ == 1 && c == '-') {
          commentDashes_ = 2;
        } else if (commentDashes_ == 2 && c == '-') {
          commentEndDashes_ = 0;
          state_ = State::Comment;
        } else if (c == '>') {
          state_ = State::Text;
        } else {
          commentDashes_ = 0;
        }
        break;

      case State::Comment:
        if (c == '-') {
          if (commentEndDashes_ < 2) ++commentEndDashes_;
        } else if (c == '>' && commentEndDashes_ >= 2) {
          state_ = State::Text;
        } else {
          commentEndDashes_ = 0;
        }
        break;

      case State::TagName:
        if (c == '>') {
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (c == '/') {
          tagIsSelfClosing_ = true;
        } else if (asciiSpace(c)) {
          state_ = State::TagAttrWait;
        } else if (tagNameLen_ < sizeof(tagName_)) {
          tagName_[tagNameLen_++] = asciiLower(c);
        }
        break;

      case State::TagAttrWait:
        if (c == '>') {
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (c == '/') {
          tagIsSelfClosing_ = true;
        } else if (!asciiSpace(c)) {
          attrNameLen_ = 0;
          attrValLen_ = 0;
          attrName_[attrNameLen_++] = asciiLower(c);
          state_ = State::TagAttrName;
        }
        break;

      case State::TagAttrName:
        if (c == '=') {
          state_ = State::TagAttrValWait;
        } else if (c == '>') {
          commitAttribute();
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (c == '/') {
          commitAttribute();
          tagIsSelfClosing_ = true;
          state_ = State::TagAttrWait;
        } else if (asciiSpace(c)) {
          state_ = State::TagAttrAfterName;
        } else if (attrNameLen_ < sizeof(attrName_)) {
          attrName_[attrNameLen_++] = asciiLower(c);
        }
        break;

      case State::TagAttrAfterName:
        if (c == '=') {
          state_ = State::TagAttrValWait;
        } else if (c == '>') {
          commitAttribute();
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (c == '/') {
          commitAttribute();
          tagIsSelfClosing_ = true;
          state_ = State::TagAttrWait;
        } else if (!asciiSpace(c)) {
          commitAttribute();
          attrName_[attrNameLen_++] = asciiLower(c);
          state_ = State::TagAttrName;
        }
        break;

      case State::TagAttrValWait:
        if (c == '"' || c == '\'') {
          attrQuote_ = c;
          state_ = State::TagAttrValQuoted;
        } else if (c == '>') {
          commitAttribute();
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (!asciiSpace(c)) {
          if (attrValLen_ < sizeof(attrVal_)) attrVal_[attrValLen_++] = c;
          state_ = State::TagAttrValUnquoted;
        }
        break;

      case State::TagAttrValQuoted:
        if (c == attrQuote_) {
          commitAttribute();
          state_ = State::TagAttrWait;
        } else if (attrValLen_ < sizeof(attrVal_)) {
          attrVal_[attrValLen_++] = c;
        }
        break;

      case State::TagAttrValUnquoted:
        if (c == '>') {
          commitAttribute();
          if (tagIsClosing_)
            finishEndTag();
          else
            finishStartTag();
          state_ = State::Text;
        } else if (asciiSpace(c)) {
          commitAttribute();
          state_ = State::TagAttrWait;
        } else if (attrValLen_ < sizeof(attrVal_)) {
          attrVal_[attrValLen_++] = c;
        }
        break;
    }
  }
  return !done_;
}

}  // namespace footnote_extract
