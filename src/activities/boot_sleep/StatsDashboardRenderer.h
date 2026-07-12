#pragma once

#include <cstdint>
#include <string>

#include "reading_stats/ReadingStats.h"

class GfxRenderer;

namespace stats_dashboard {

struct DashboardData {
  std::string title;
  std::string chapter;
  std::string imagePath;
  reading_stats::ReadingStatsData book;
  reading_stats::ReadingStatsData global;
  uint8_t progressPercent = 0;
  uint32_t todayDay = 0;
};

// Draws one complete CrossInk-style dashboard sleep frame. The image may be a
// normal BMP cover/wallpaper or a full-panel PXC wallpaper. PXC is streamed and
// scaled into the cover box without allocating a full image buffer.
bool render(GfxRenderer& renderer, const DashboardData& data);

}  // namespace stats_dashboard
