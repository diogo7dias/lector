#pragma once

#include <cstdint>
#include <cstring>

// Parses an HTML colspan/rowspan attribute value.
//
// A missing, empty or malformed value means 1, i.e. no span: HTML asks reading systems to
// ignore what they cannot parse rather than guess. rowspan="0" means "to the end of the table
// group", which has no numeric bound, so it is represented as UINT16_MAX; an out-of-range
// number saturates to the same value, since both mean "more rows than any real table has".
//
// Header-only and dependency-free so the parser and its test share one definition.
inline uint16_t parseTableSpan(const char* value) {
  if (!value || value[0] == '\0') return 1;

  uint32_t span = 0;
  for (const char* current = value; *current != '\0'; ++current) {
    if (*current < '0' || *current > '9') return 1;
    const uint32_t digit = static_cast<uint32_t>(*current - '0');
    if (span > (UINT16_MAX - digit) / 10) return UINT16_MAX;
    span = span * 10 + digit;
  }
  return span == 0 ? UINT16_MAX : static_cast<uint16_t>(span);
}

// Extracts a numeric span attribute (e.g. "colspan") from a lowercased raw opening tag buffer.
// Returns 1 when absent or malformed.
inline uint16_t parseTagAttributeSpan(const char* tagText, const char* attrName) {
  if (!tagText || !attrName) return 1;
  const size_t nameLen = std::strlen(attrName);
  const char* p = tagText;
  while ((p = std::strstr(p, attrName)) != nullptr) {
    // Require whitespace before attribute name so "mycolspan" does not match.
    if (p > tagText && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\r' && p[-1] != '\n') {
      p += nameLen;
      continue;
    }
    p += nameLen;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '=') continue;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    char quote = 0;
    if (*p == '"' || *p == '\'') quote = *p++;
    char digits[16];
    size_t n = 0;
    while (*p && (quote ? *p != quote : (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '/'))) {
      if (n + 1 < sizeof(digits)) digits[n++] = *p;
      ++p;
    }
    digits[n] = '\0';
    return parseTableSpan(digits);
  }
  return 1;
}
