#pragma once

#include <cstdio>
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
  int count = 0;
  long long at = 0;
  int consumed = 0;
  if (sscanf(text, "%d %lld\n%n", &count, &at, &consumed) < 2 || consumed <= 0) return saved;
  std::string url(text + consumed);
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
