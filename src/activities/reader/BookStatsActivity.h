#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "activities/Activity.h"
#include "reading_stats/ReadingStats.h"

struct Rect;

class BookStatsActivity final : public Activity {
 public:
  using ResetHandler = std::function<bool(bool resetAll, reading_stats::ReadingStatsData& bookStats,
                                          reading_stats::ReadingStatsData& globalStats)>;

  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                    reading_stats::ReadingStatsData bookStats, reading_stats::ReadingStatsData globalStats,
                    uint8_t progressPercent, uint32_t estimatedTimeLeftSeconds, ResetHandler resetHandler);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Page : uint8_t { CurrentBook, AllBooks };

  void drawCurrentBook(const Rect& screen, int contentTop, int contentBottom);
  void drawAllBooks(const Rect& screen, int contentTop, int contentBottom);
  void drawMetricGrid(const Rect& area, const std::array<std::pair<std::string, const char*>, 6>& metrics);
  void drawTimeOfDayChart(const Rect& area, const reading_stats::ReadingStatsData& stats);
  void drawWeekdayChart(const Rect& area, const reading_stats::ReadingStatsData& stats);
  void confirmReset();

  std::string title_;
  reading_stats::ReadingStatsData bookStats_;
  reading_stats::ReadingStatsData globalStats_;
  uint8_t progressPercent_ = 0;
  uint32_t estimatedTimeLeftSeconds_ = 0;
  ResetHandler resetHandler_;
  Page page_ = Page::CurrentBook;
};
