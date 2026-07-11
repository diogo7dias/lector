#include "ReadingStatsPresentation.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace reading_stats {

std::string formatDuration(const uint32_t seconds) {
  if (seconds == 0) return "0 min";
  if (seconds < 60) return "< 1 min";
  const uint32_t minutes = seconds / 60;
  if (minutes < 60) return std::to_string(minutes) + " min";
  const uint32_t hours = minutes / 60;
  const uint32_t remainingMinutes = minutes % 60;
  if (remainingMinutes == 0) return std::to_string(hours) + "h";
  return std::to_string(hours) + "h " + std::to_string(remainingMinutes) + "m";
}

float pagesPerMinute(const uint32_t pages, const uint32_t seconds) {
  return seconds == 0 ? 0.0f : static_cast<float>(pages) * 60.0f / static_cast<float>(seconds);
}

uint32_t estimateTimeLeft(const uint32_t readingSeconds, const uint8_t progressPercent) {
  if (readingSeconds == 0 || progressPercent == 0 || progressPercent >= 100) return 0;
  const uint64_t estimate = static_cast<uint64_t>(readingSeconds) * (100u - progressPercent) / progressPercent;
  return estimate > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                         : static_cast<uint32_t>(estimate);
}

uint32_t estimateFinishDay(const uint32_t todayDay, const uint32_t startDay, const uint32_t readingSeconds,
                           const uint32_t timeLeftSeconds) {
  if (startDay == 0 || todayDay < startDay || readingSeconds == 0 || timeLeftSeconds == 0) return 0;
  const uint32_t observedDays = todayDay - startDay + 1u;
  const uint32_t dailySeconds = std::max<uint32_t>(1, readingSeconds / observedDays);
  const uint32_t daysLeft = (timeLeftSeconds + dailySeconds - 1u) / dailySeconds;
  if (daysLeft > std::numeric_limits<uint32_t>::max() - todayDay) return std::numeric_limits<uint32_t>::max();
  return todayDay + daysLeft;
}

}  // namespace reading_stats
