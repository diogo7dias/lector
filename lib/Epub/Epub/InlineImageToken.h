#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// A small image laid out inside a text line travels through ParsedText as this word:
// "\x01I" + id, width, height as 4 hex digits each. extractLine removes it before the
// TextBlock is built, so it never reaches section.bin; the image itself becomes a PageImage.
inline std::string makeInlineImageToken(const uint16_t id, const uint16_t width, const uint16_t height) {
  char token[15];
  snprintf(token, sizeof(token), "\x01I%04X%04X%04X", id, width, height);
  return token;
}

inline bool parseInlineImageToken(const std::string& token, uint16_t& id, uint16_t& width, uint16_t& height) {
  if (token.size() != 14 || token[0] != '\x01' || token[1] != 'I') return false;
  uint16_t values[3] = {};
  for (int i = 0; i < 12; ++i) {
    const char c = token[2 + i];
    const int nibble = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (nibble < 0) return false;
    values[i / 4] = static_cast<uint16_t>((values[i / 4] << 4) | nibble);
  }
  if (values[1] == 0 || values[2] == 0) return false;
  id = values[0];
  width = values[1];
  height = values[2];
  return true;
}

inline bool isInlineImageToken(const std::string& token) {
  uint16_t id, width, height;
  return parseInlineImageToken(token, id, width, height);
}
