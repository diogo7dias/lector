#pragma once

#include <cstdint>
#include <string_view>

namespace stats_dashboard {

// Mirrors CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD without pulling the
// settings header into the renderer. A static_assert in SleepActivity.cpp keeps the
// two values in step, so a reorder of the settings enum fails the build rather than
// silently mapping the dashboard onto another sleep face.
inline constexpr uint8_t kStatsDashboardMode = 7;

constexpr bool isDashboardMode(const uint8_t mode) { return mode == kStatsDashboardMode; }

constexpr char asciiLower(const char value) { return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value; }

constexpr bool endsWithIgnoringCase(const std::string_view value, const std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t index = 0; index < suffix.size(); ++index) {
    if (asciiLower(value[offset + index]) != asciiLower(suffix[index])) return false;
  }
  return true;
}

constexpr bool supportsBook(const std::string_view path) {
  return endsWithIgnoringCase(path, ".epub") || endsWithIgnoringCase(path, ".xtc") ||
         endsWithIgnoringCase(path, ".xtch") || endsWithIgnoringCase(path, ".txt") || endsWithIgnoringCase(path, ".md");
}

}  // namespace stats_dashboard
