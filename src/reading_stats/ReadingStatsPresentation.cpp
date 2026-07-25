#include "ReadingStatsPresentation.h"

#include <algorithm>
#include <cmath>
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

int centeredTextX(const int areaX, const int areaWidth, const int textWidth) {
  return areaX + std::max(0, (areaWidth - textWidth) / 2);
}

int chartLabelColumnWidth(const int areaWidth, const int widestLabelWidth) {
  constexpr int kLabelGap = 8;
  constexpr int kMinimumBarWidth = 24;
  return std::min(widestLabelWidth + kLabelGap, std::max(0, areaWidth - kMinimumBarWidth));
}

HorizontalLayout insetHorizontal(const int areaX, const int areaWidth, const int sideMargin) {
  const int inset = std::clamp(sideMargin, 0, std::max(0, areaWidth) / 2);
  return HorizontalLayout{areaX + inset, std::max(0, areaWidth - inset * 2)};
}

DashboardLayout dashboardLayout(const int screenWidth, const int screenHeight) {
  constexpr int kNarrowInset = 20;
  constexpr int kWideInset = 75;
  constexpr int kTop = 70;
  constexpr int kCoverWidth = 296;
  constexpr int kCoverHeight = 444;
  constexpr int kNarrowStatsWidth = 105;
  constexpr int kWideStatsWidth = 120;
  constexpr int kGap = 15;
  constexpr int kWideShift = 15;
  constexpr int kFooterBottom = 97;
  const bool wide = screenWidth >= 560;
  const int inset = wide ? kWideInset : kNarrowInset;
  const int statsWidth = wide ? kWideStatsWidth : kNarrowStatsWidth;
  const int maxCoverWidth = std::max(1, screenWidth - inset * 2 - statsWidth - kGap);
  const int coverWidth = std::min(kCoverWidth, maxCoverWidth);
  const int coverHeight = std::min(kCoverHeight, coverWidth * 3 / 2);
  return DashboardLayout{inset + (wide ? kWideShift : 0),
                         kTop,
                         coverWidth,
                         coverHeight,
                         screenWidth - inset - (wide ? kWideShift : 0),
                         std::max(0, screenHeight - kFooterBottom)};
}

TimeOfDay dominantTimeOfDay(const std::array<uint32_t, kTimeOfDayBucketCount>& buckets) {
  const auto found = std::max_element(buckets.begin(), buckets.end());
  return static_cast<TimeOfDay>(std::distance(buckets.begin(), found));
}

std::string formatShortDate(const uint32_t dayIndex) {
  if (dayIndex == 0) return "-";
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  CalendarDate date;
  if (!dateFromDayIndex(dayIndex, date) || date.month < 1 || date.month > 12) return "-";
  return std::string(kMonths[date.month - 1]) + " " + std::to_string(date.day);
}

uint32_t averagePerObservedDay(const uint32_t seconds, const uint32_t startDay, const uint32_t endDay) {
  if (startDay == 0 || endDay < startDay) return 0;
  return seconds / (endDay - startDay + 1u);
}

DashboardImageRect fitDashboardImage(const int sourceWidth, const int sourceHeight, const DashboardImageRect target) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || target.width <= 0 || target.height <= 0) return target;
  const double scale =
      std::min(static_cast<double>(target.width) / sourceWidth, static_cast<double>(target.height) / sourceHeight);
  const int width = std::min(target.width, std::max(1, static_cast<int>(std::ceil(sourceWidth * scale))));
  const int height = std::min(target.height, std::max(1, static_cast<int>(std::ceil(sourceHeight * scale))));
  return DashboardImageRect{target.x + (target.width - width) / 2, target.y + (target.height - height) / 2, width,
                            height};
}

int mapDashboardPixel(const int destinationPixel, const int destinationSize, const int sourceSize) {
  if (destinationSize <= 1 || sourceSize <= 1) return 0;
  return std::clamp(destinationPixel, 0, destinationSize - 1) * (sourceSize - 1) / (destinationSize - 1);
}

}  // namespace reading_stats
