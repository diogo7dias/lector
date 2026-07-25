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

// Draws one complete dashboard sleep frame: the book cover scaled into the cover
// box, the reading-stats column beside it, and the title/chapter/streak chrome.
// `imagePath` must be a BMP (a generated cover or cover thumb).
bool render(GfxRenderer& renderer, const DashboardData& data);

}  // namespace stats_dashboard
