#include "DictInlineFields.h"

#include <Arduino.h>
#include <Logging.h>

#include <cstring>

namespace {

constexpr char PRE_OPEN[] = "<pre>";
constexpr char PRE_CLOSE[] = "</pre>";

const char* htmlEscapeFor(const char c) {
  switch (c) {
    case '&':
      return "&amp;";
    case '<':
      return "&lt;";
    case '>':
      return "&gt;";
    default:
      return nullptr;
  }
}

bool isMarkupType(const char type) { return type == 'h' || type == 'x'; }

// Takes the next field off `input`. A binary field leaves `field` empty.
bool readField(std::string_view& input, char& type, std::string_view& field, const bool truncated) {
  type = input.front();
  input.remove_prefix(1);
  field = {};
  if (type >= 'A' && type <= 'Z') {
    if (input.size() < 4) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(input.data());
    const size_t size = (static_cast<size_t>(p[0]) << 24) | (static_cast<size_t>(p[1]) << 16) |
                        (static_cast<size_t>(p[2]) << 8) | static_cast<size_t>(p[3]);
    input.remove_prefix(4);
    if (size > input.size()) return false;
    input.remove_prefix(size);
    return true;
  }
  if (type < 'a' || type > 'z') return false;
  const size_t end = input.find('\0');
  if (end == std::string_view::npos && !truncated) return false;
  field = input.substr(0, end);
  input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
  return true;
}

}  // namespace

size_t escapedHtmlSize(const std::string_view text) {
  size_t size = 0;
  for (const char c : text) {
    const char* escape = htmlEscapeFor(c);
    size += escape ? strlen(escape) : 1;
  }
  return size;
}

void appendEscapedHtml(std::string& out, const std::string_view text) {
  for (const char c : text) {
    if (const char* escape = htmlEscapeFor(c)) {
      out += escape;
    } else {
      out.push_back(c);
    }
  }
}

InlineDecode decodeInlineFields(std::string& text, const size_t start, const bool truncated) {
  const auto source = std::string_view(text).substr(start);
  size_t decodedSize = start;
  bool onlyMarkup = true;
  char type;
  std::string_view field;
  for (auto input = source; !input.empty();) {
    if (!readField(input, type, field, truncated)) {
      LOG_ERR("DICT", "Invalid inline definition field");
      return InlineDecode::Damaged;
    }
    if (field.empty()) continue;
    if (isMarkupType(type)) {
      decodedSize += field.size();
    } else {
      onlyMarkup = false;
      decodedSize += sizeof(PRE_OPEN) - 1 + escapedHtmlSize(field) + sizeof(PRE_CLOSE) - 1;
    }
  }
  if (decodedSize == start) return InlineDecode::Empty;

  // Dropping the framing only shrinks the text, so no second buffer is needed.
  if (onlyMarkup) {
    size_t written = start;
    for (auto input = source; !input.empty();) {
      readField(input, type, field, truncated);
      if (field.empty()) continue;
      memmove(text.data() + written, field.data(), field.size());
      written += field.size();
    }
    text.resize(written);
    return InlineDecode::Ok;
  }

  if (ESP.getMaxAllocHeap() < decodedSize + 1 + DEFINITION_HEAP_HEADROOM_BYTES) {
    LOG_ERR("DICT", "Low heap for %u byte decoded definition", static_cast<unsigned>(decodedSize));
    return InlineDecode::LowMemory;
  }
  std::string decoded;
  decoded.reserve(decodedSize);
  decoded.append(text.data(), start);
  for (auto input = source; !input.empty();) {
    readField(input, type, field, truncated);
    if (field.empty()) continue;
    if (isMarkupType(type)) {
      decoded.append(field);
    } else {
      decoded += PRE_OPEN;
      appendEscapedHtml(decoded, field);
      decoded += PRE_CLOSE;
    }
  }
  text.swap(decoded);
  return InlineDecode::Ok;
}
