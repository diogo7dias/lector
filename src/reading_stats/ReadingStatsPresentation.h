#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "ReadingStats.h"

namespace reading_stats {

struct HorizontalLayout {
  int x;
  int width;
};

struct DashboardLayout {
  int coverX;
  int coverY;
  int coverWidth;
  int coverHeight;
  int statsRightX;
  int footerY;
};

struct DashboardImageRect {
  int x;
  int y;
  int width;
  int height;
  bool operator==(const DashboardImageRect&) const = default;
};

std::string formatDuration(uint32_t seconds);
float pagesPerMinute(uint32_t pages, uint32_t seconds);
uint32_t estimateTimeLeft(uint32_t readingSeconds, uint8_t progressPercent);
uint32_t estimateFinishDay(uint32_t todayDay, uint32_t startDay, uint32_t readingSeconds, uint32_t timeLeftSeconds);
int centeredTextX(int areaX, int areaWidth, int textWidth);
int chartLabelColumnWidth(int areaWidth, int widestLabelWidth);
HorizontalLayout insetHorizontal(int areaX, int areaWidth, int sideMargin);
DashboardLayout dashboardLayout(int screenWidth, int screenHeight);
TimeOfDay dominantTimeOfDay(const std::array<uint32_t, kTimeOfDayBucketCount>& buckets);
std::string formatShortDate(uint32_t dayIndex);
uint32_t averagePerObservedDay(uint32_t seconds, uint32_t startDay, uint32_t endDay);
DashboardImageRect fitDashboardImage(int sourceWidth, int sourceHeight, DashboardImageRect target);
int mapDashboardPixel(int destinationPixel, int destinationSize, int sourceSize);

}  // namespace reading_stats
