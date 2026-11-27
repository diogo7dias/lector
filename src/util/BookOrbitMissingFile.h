#pragma once

#include <cctype>
#include <cstdlib>
#include <string>

// The sleep check's save file, kept pure so the host tests reach it. Line 1 is
// "<count> <epoch of the check>", line 2 the Reading feed URL.
namespace bookorbit_missing_file {

struct Saved {
  int count = 0;
  long long checkedAt = 0;
  std::string feedUrl;
};

// Parsed, not trusted: a torn or foreign file reads as nothing missing.
inline Saved parse(const char* text) {
  Saved saved;
  // strtol, not sscanf: sscanf links newlib's float-capable scanf (about 15 KB of flash).
  // Both skip leading whitespace and take a sign; the URL starts after the next whitespace run.
  char* end = nullptr;
  const int count = static_cast<int>(strtol(text, &end, 10));
  if (end == text) return saved;
  const char* p = end;
  const long long at = strtoll(p, &end, 10);
  if (end == p) return saved;
  p = end;
  while (isspace(static_cast<unsigned char>(*p))) p++;
  std::string url(p);
  while (!url.empty() && (url.back() == '\n' || url.back() == '\r')) url.pop_back();
  if (count < 0 || url.empty()) return saved;
  saved.count = count;
  saved.checkedAt = at;
  saved.feedUrl = std::move(url);
  return saved;
}

constexpr long long CHECK_EVERY_S = 6 * 60 * 60;

// Always when WiFi is already up; otherwise every CHECK_EVERY_S, and never while
// `now` is 0 (a clock that was never set). A clock that went back counts as due.
inline bool due(const bool wifiUp, const long long now, const long long checkedAt) {
  if (wifiUp) return true;
  if (now == 0) return false;
  return checkedAt <= 0 || now < checkedAt || now - checkedAt >= CHECK_EVERY_S;
}

}  // namespace bookorbit_missing_file
