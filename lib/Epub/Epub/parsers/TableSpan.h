#pragma once

#include <cstdint>

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
