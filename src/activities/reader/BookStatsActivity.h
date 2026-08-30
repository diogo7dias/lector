#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "activities/Activity.h"
#include "components/StatsDashboardLayout.h"
#include "components/UiAppHost.h"
#include "reading_stats/ReadingStats.h"

struct Rect;

class BookStatsActivity final : public Activity, protected UiAppHost {
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

  // Six values, each with the word for what it counts. Built per page, then
  // handed to the grid as FreeInkUI metric cards.
  using MetricRow = std::pair<std::string, const char*>;

  void buildScreen(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  void buildCurrentBook(UiScreen& screen);
  void buildAllBooks(UiScreen& screen);
  void buildMetricGrid(UiScreen& screen, const stats_dashboard::Rect& area, const std::array<MetricRow, 6>& metrics);
  void buildDateCards(UiScreen& screen, const stats_dashboard::Rect& area);
  void buildTimeOfDayChart(UiScreen& screen, const stats_dashboard::Rect& area,
                           const reading_stats::ReadingStatsData& stats);
  void buildWeekdayChart(UiScreen& screen, const stats_dashboard::Rect& area,
                         const reading_stats::ReadingStatsData& stats);
  void confirmReset();

  std::string title_;
  reading_stats::ReadingStatsData bookStats_;
  reading_stats::ReadingStatsData globalStats_;
  uint8_t progressPercent_ = 0;
  uint32_t estimatedTimeLeftSeconds_ = 0;
  ResetHandler resetHandler_;
  Page page_ = Page::CurrentBook;
};
