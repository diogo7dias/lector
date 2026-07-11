#include "BookStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsPresentation.h"

using reading_stats::ReadingStatsData;

namespace {

std::string formatRate(const ReadingStatsData& stats) {
  char value[16];
  snprintf(value, sizeof(value), "%.1f",
           reading_stats::pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  return value;
}

std::string formatDate(const uint32_t dayIndex) {
  if (dayIndex == 0) return "-";
  static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  reading_stats::CalendarDate date;
  if (!reading_stats::dateFromDayIndex(dayIndex, date)) return "-";
  char value[16];
  snprintf(value, sizeof(value), "%s %u", months[date.month - 1], static_cast<unsigned>(date.day));
  return value;
}

uint32_t maximum(const std::array<uint32_t, reading_stats::kTimeOfDayBucketCount>& values) {
  return *std::max_element(values.begin(), values.end());
}

uint32_t maximum(const std::array<uint32_t, reading_stats::kDayOfWeekCount>& values) {
  return *std::max_element(values.begin(), values.end());
}

}  // namespace

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                     ReadingStatsData bookStats, ReadingStatsData globalStats,
                                     const uint8_t progressPercent, const uint32_t estimatedTimeLeftSeconds,
                                     ResetHandler resetHandler)
    : Activity("BookStats", renderer, mappedInput),
      title_(std::move(title)),
      bookStats_(bookStats),
      globalStats_(globalStats),
      progressPercent_(std::min<uint8_t>(progressPercent, 100)),
      estimatedTimeLeftSeconds_(estimatedTimeLeftSeconds),
      resetHandler_(std::move(resetHandler)) {}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  page_ = Page::CurrentBook;
  requestUpdate();
}

void BookStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (page_ == Page::AllBooks) {
      page_ = Page::CurrentBook;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (page_ == Page::CurrentBook) {
      page_ = Page::AllBooks;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (page_ == Page::AllBooks) {
      page_ = Page::CurrentBook;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) confirmReset();
}

void BookStatsActivity::confirmReset() {
  const bool resetAll = page_ == Page::AllBooks;
  startActivityForResult(makeUniqueNoThrow<ConfirmationActivity>(
                             renderer, mappedInput, resetAll ? tr(STR_STATS_RESET_ALL) : tr(STR_STATS_RESET_BOOK),
                             tr(STR_STATS_RESET_WARNING)),
                         [this, resetAll](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           ReadingStatsData book;
                           ReadingStatsData global;
                           if (resetHandler_ && resetHandler_(resetAll, book, global)) {
                             bookStats_ = book;
                             globalStats_ = global;
                             estimatedTimeLeftSeconds_ =
                                 reading_stats::estimateTimeLeft(bookStats_.totalReadingSeconds, progressPercent_);
                           } else {
                             GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
                           }
                         });
}

void BookStatsActivity::drawMetricGrid(const Rect& area,
                                       const std::array<std::pair<std::string, const char*>, 6>& metrics) {
  const int columns = 3;
  const int rows = 2;
  const int cellW = area.width / columns;
  const int cellH = area.height / rows;
  for (int index = 0; index < static_cast<int>(metrics.size()); ++index) {
    const int column = index % columns;
    const int row = index / columns;
    const Rect cell{area.x + column * cellW, area.y + row * cellH,
                    column == columns - 1 ? area.width - column * cellW : cellW,
                    row == rows - 1 ? area.height - row * cellH : cellH};
    renderer.drawRect(cell.x, cell.y, cell.width, cell.height);
    const std::string value =
        renderer.truncatedText(UI_12_FONT_ID, metrics[index].first.c_str(), cell.width - 6, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, cell.y + 5, value.c_str(), true, EpdFontFamily::BOLD);
    const std::string label = renderer.truncatedText(SMALL_FONT_ID, metrics[index].second, cell.width - 4);
    renderer.drawCenteredText(SMALL_FONT_ID, cell.y + cellH / 2 + 4, label.c_str());
  }
}

void BookStatsActivity::drawTimeOfDayChart(const Rect& area, const ReadingStatsData& stats) {
  renderer.drawText(UI_10_FONT_ID, area.x, area.y, tr(STR_STATS_TIME_OF_DAY), true, EpdFontFamily::BOLD);
  const std::array<const char*, 4> labels = {tr(STR_STATS_MORNING), tr(STR_STATS_AFTERNOON), tr(STR_STATS_EVENING),
                                             tr(STR_STATS_NIGHT)};
  const int top = area.y + renderer.getLineHeight(UI_10_FONT_ID) + 2;
  const int rowH = std::max(8, (area.height - (top - area.y)) / 4);
  const int labelW = std::min(75, area.width / 4);
  const uint32_t maxValue = maximum(stats.timeOfDaySeconds);
  for (int index = 0; index < 4; ++index) {
    const int y = top + index * rowH;
    renderer.drawText(SMALL_FONT_ID, area.x, y, labels[index]);
    const int barX = area.x + labelW;
    const int barW = area.width - labelW;
    renderer.drawRect(barX, y + 2, barW, std::max(4, rowH - 5));
    if (maxValue > 0 && stats.timeOfDaySeconds[index] > 0) {
      const int fill =
          std::max(1, static_cast<int>(static_cast<uint64_t>(barW) * stats.timeOfDaySeconds[index] / maxValue));
      renderer.fillRect(barX, y + 2, fill, std::max(4, rowH - 5), true);
    }
  }
}

void BookStatsActivity::drawWeekdayChart(const Rect& area, const ReadingStatsData& stats) {
  renderer.drawText(UI_10_FONT_ID, area.x, area.y, tr(STR_STATS_DAY_OF_WEEK), true, EpdFontFamily::BOLD);
  const std::array<const char*, 7> labels = {tr(STR_STATS_MON), tr(STR_STATS_TUE), tr(STR_STATS_WED), tr(STR_STATS_THU),
                                             tr(STR_STATS_FRI), tr(STR_STATS_SAT), tr(STR_STATS_SUN)};
  const int top = area.y + renderer.getLineHeight(UI_10_FONT_ID) + 2;
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  const int chartH = std::max(8, area.height - (top - area.y) - labelH);
  const int slotW = area.width / 7;
  const uint32_t maxValue = maximum(stats.dayOfWeekSeconds);
  for (int index = 0; index < 7; ++index) {
    const int barW = std::max(3, slotW / 2);
    const int x = area.x + index * slotW + (slotW - barW) / 2;
    const int fillH =
        maxValue == 0 ? 0 : static_cast<int>(static_cast<uint64_t>(chartH) * stats.dayOfWeekSeconds[index] / maxValue);
    renderer.drawRect(x, top, barW, chartH);
    if (fillH > 0) renderer.fillRect(x, top + chartH - fillH, barW, fillH, true);
    const int labelX = area.x + index * slotW + (slotW - renderer.getTextWidth(SMALL_FONT_ID, labels[index])) / 2;
    renderer.drawText(SMALL_FONT_ID, labelX, top + chartH + 1, labels[index]);
  }
}

void BookStatsActivity::drawCurrentBook(const Rect& screen, const int contentTop, const int contentBottom) {
  int y = contentTop;
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, title_.c_str(), screen.width - 16, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, y, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 3;

  char progress[16];
  snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(progressPercent_));
  renderer.drawText(SMALL_FONT_ID, screen.x, y, tr(STR_STATS_PROGRESS));
  renderer.drawText(SMALL_FONT_ID, screen.x + screen.width - renderer.getTextWidth(SMALL_FONT_ID, progress), y,
                    progress);
  y += renderer.getLineHeight(SMALL_FONT_ID) + 2;
  renderer.drawRect(screen.x, y, screen.width, 10);
  renderer.fillRect(screen.x, y, screen.width * progressPercent_ / 100, 10, true);
  y += 15;

  const uint32_t averageSession =
      bookStats_.totalSessions == 0 ? 0 : bookStats_.totalReadingSeconds / bookStats_.totalSessions;
  const std::array<std::pair<std::string, const char*>, 6> values = {{
      {std::to_string(bookStats_.totalSessions), tr(STR_STATS_SESSIONS)},
      {reading_stats::formatDuration(bookStats_.totalReadingSeconds), tr(STR_STATS_READING_TIME)},
      {std::to_string(bookStats_.totalPagesTurned), tr(STR_STATS_PAGES_TURNED)},
      {reading_stats::formatDuration(averageSession), tr(STR_STATS_AVG_SESSION)},
      {formatRate(bookStats_), tr(STR_STATS_PAGES_PER_MIN)},
      {reading_stats::formatDuration(estimatedTimeLeftSeconds_), tr(STR_STATS_TIME_LEFT)},
  }};
  const int gridH = std::min(130, std::max(86, (contentBottom - y) / 3));
  drawMetricGrid(Rect{screen.x, y, screen.width, gridH}, values);
  y += gridH + 5;

  const auto now = reading_stats::currentLocalDateTime();
  const uint32_t endDay =
      bookStats_.completed
          ? bookStats_.finishedDay
          : (now.valid ? reading_stats::estimateFinishDay(now.dayIndex, bookStats_.startDay,
                                                          bookStats_.totalReadingSeconds, estimatedTimeLeftSeconds_)
                       : 0);
  const int dateH = renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 5;
  const int halfW = screen.width / 2;
  const std::string startDate = formatDate(bookStats_.startDay);
  const std::string endDate = formatDate(endDay);
  const char* endLabel = bookStats_.completed ? tr(STR_STATS_FINISHED) : tr(STR_STATS_EST_FINISH);
  renderer.drawRect(screen.x, y, halfW, dateH);
  renderer.drawRect(screen.x + halfW, y, screen.width - halfW, dateH);
  renderer.drawCenteredText(UI_10_FONT_ID, y + 2, startDate.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, y + renderer.getLineHeight(UI_10_FONT_ID), tr(STR_STATS_STARTED));
  renderer.drawText(
      UI_10_FONT_ID,
      screen.x + halfW +
          (screen.width - halfW - renderer.getTextWidth(UI_10_FONT_ID, endDate.c_str(), EpdFontFamily::BOLD)) / 2,
      y + 2, endDate.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID,
                    screen.x + halfW + (screen.width - halfW - renderer.getTextWidth(SMALL_FONT_ID, endLabel)) / 2,
                    y + renderer.getLineHeight(UI_10_FONT_ID), endLabel);
  y += dateH + 4;

  const int remaining = contentBottom - y;
  const int chartH = remaining / 2;
  drawTimeOfDayChart(Rect{screen.x, y, screen.width, chartH}, bookStats_);
  drawWeekdayChart(Rect{screen.x, y + chartH, screen.width, remaining - chartH}, bookStats_);
}

void BookStatsActivity::drawAllBooks(const Rect& screen, const int contentTop, const int contentBottom) {
  int y = contentTop;
  const uint32_t averageSession =
      globalStats_.totalSessions == 0 ? 0 : globalStats_.totalReadingSeconds / globalStats_.totalSessions;
  const std::array<std::pair<std::string, const char*>, 6> values = {{
      {std::to_string(globalStats_.totalSessions), tr(STR_STATS_SESSIONS)},
      {reading_stats::formatDuration(globalStats_.totalReadingSeconds), tr(STR_STATS_READING_TIME)},
      {std::to_string(globalStats_.totalPagesTurned), tr(STR_STATS_PAGES_TURNED)},
      {reading_stats::formatDuration(averageSession), tr(STR_STATS_AVG_SESSION)},
      {formatRate(globalStats_), tr(STR_STATS_PAGES_PER_MIN)},
      {std::to_string(globalStats_.completedBooks), tr(STR_STATS_COMPLETED)},
  }};
  const int gridH = std::min(150, std::max(92, (contentBottom - y) / 3));
  drawMetricGrid(Rect{screen.x, y, screen.width, gridH}, values);
  y += gridH + 4;

  const auto now = reading_stats::currentLocalDateTime();
  const uint32_t today = now.valid ? now.dayIndex : globalStats_.readingHistoryAnchorDay;
  char streak[40];
  snprintf(streak, sizeof(streak), "%s: %u | %s: %u", tr(STR_STATS_STREAK),
           static_cast<unsigned>(globalStats_.currentStreak(today)), tr(STR_STATS_BEST),
           static_cast<unsigned>(globalStats_.longestReadingStreak));
  renderer.drawCenteredText(UI_10_FONT_ID, y, streak, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 3;

  const int remaining = contentBottom - y;
  const int chartH = remaining / 2;
  drawTimeOfDayChart(Rect{screen.x, y, screen.width, chartH}, globalStats_);
  drawWeekdayChart(Rect{screen.x, y + chartH, screen.width, remaining - chartH}, globalStats_);
}

void BookStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  const char* pageTitle = page_ == Page::CurrentBook ? tr(STR_STATS_CURRENT_BOOK) : tr(STR_STATS_ALL_BOOKS);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 pageTitle);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = screen.y + screen.height - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (page_ == Page::CurrentBook) {
    drawCurrentBook(screen, contentTop, contentBottom);
  } else {
    drawAllBooks(screen, contentTop, contentBottom);
  }

  const auto labels = page_ == Page::CurrentBook
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_RESET), "", tr(STR_STATS_MORE))
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_RESET), tr(STR_STATS_BOOK), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
