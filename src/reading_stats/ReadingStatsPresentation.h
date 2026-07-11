#pragma once

#include <cstdint>
#include <string>

namespace reading_stats {

struct HorizontalLayout {
  int x;
  int width;
};

std::string formatDuration(uint32_t seconds);
float pagesPerMinute(uint32_t pages, uint32_t seconds);
uint32_t estimateTimeLeft(uint32_t readingSeconds, uint8_t progressPercent);
uint32_t estimateFinishDay(uint32_t todayDay, uint32_t startDay, uint32_t readingSeconds, uint32_t timeLeftSeconds);
int centeredTextX(int areaX, int areaWidth, int textWidth);
int chartLabelColumnWidth(int areaWidth, int widestLabelWidth);
HorizontalLayout insetHorizontal(int areaX, int areaWidth, int sideMargin);

}  // namespace reading_stats
