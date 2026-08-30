#include "BookStatsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/StatsDashboardLayout.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsPresentation.h"

namespace fui = freeink::ui;

using reading_stats::ReadingStatsData;

namespace {

constexpr int STATS_SIDE_MARGIN = 10;

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

// Everything below the metric grid is keyed off a calendar date. Without a clock the
// device cannot supply one, so the rows would sit there as dashes and empty boxes with
// no way for the reader to tell a broken screen from an unset clock.
bool datesAreUnavailable(const ReadingStatsData& stats) {
  return !reading_stats::hasDatedActivity(stats) && !halClock.hasDate();
}

fui::Rect toFui(const stats_dashboard::Rect& rect) {
  return fui::Rect{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                   static_cast<int16_t>(rect.height)};
}

// A boxed value with its word under it, the shape every number on this screen
// is written in.
void card(UiAppHost::UiScreen& screen, const stats_dashboard::Rect& rect, const char* value, const char* label) {
  fui::MetricCardProps props;
  props.value = value;
  props.caption = label;
  props.valueText = screen.theme().bodyText;
  props.captionText = screen.theme().smallText;
  props.styles = screen.theme().button;
  fui::metricCard(screen.frame(), toFui(rect), props);
}

void centred(UiAppHost::UiScreen& screen, const stats_dashboard::Rect& rect, const char* text, fui::TextStyle style) {
  style.align = fui::TextAlign::Center;
  screen.frame().target().text(toFui(rect), text, style);
}

}  // namespace

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                     ReadingStatsData bookStats, ReadingStatsData globalStats,
                                     const uint8_t progressPercent, const uint32_t estimatedTimeLeftSeconds,
                                     ResetHandler resetHandler)
    : Activity("BookStats", renderer, mappedInput),
      UiAppHost(renderer),
      title_(std::move(title)),
      bookStats_(bookStats),
      globalStats_(globalStats),
      progressPercent_(std::min<uint8_t>(progressPercent, 100)),
      estimatedTimeLeftSeconds_(estimatedTimeLeftSeconds),
      resetHandler_(std::move(resetHandler)) {}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  page_ = Page::CurrentBook;
  resetUi();
  app.setScreen(&BookStatsActivity::screenTrampoline, this);
  requestUpdate();
}

void BookStatsActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<BookStatsActivity*>(user)->buildScreen(screen);
}

void BookStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmReset();
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

void BookStatsActivity::buildMetricGrid(UiScreen& screen, const stats_dashboard::Rect& area,
                                        const std::array<MetricRow, 6>& metrics) {
  constexpr int kColumns = 3;
  constexpr int kRows = 2;
  for (int index = 0; index < static_cast<int>(metrics.size()); ++index) {
    card(screen, stats_dashboard::metricCell(area, index, kColumns, kRows), metrics[index].first.c_str(),
         metrics[index].second);
  }
}

void BookStatsActivity::buildTimeOfDayChart(UiScreen& screen, const stats_dashboard::Rect& area,
                                            const ReadingStatsData& stats) {
  auto& target = screen.frame().target();
  const auto& theme = screen.theme();
  const int headingHeight = target.lineHeight(theme.bodyText.font);
  fui::TextStyle heading = theme.bodyText;
  heading.align = fui::TextAlign::Left;
  target.text(toFui(stats_dashboard::Rect{area.x, area.y, area.width, headingHeight}), tr(STR_STATS_TIME_OF_DAY),
              heading);

  const std::array<const char*, 4> labels = {tr(STR_STATS_MORNING), tr(STR_STATS_AFTERNOON), tr(STR_STATS_EVENING),
                                             tr(STR_STATS_NIGHT)};
  const stats_dashboard::Rect rows{area.x, area.y + headingHeight + 2, area.width, area.height - headingHeight - 2};
  const int rowHeight = stats_dashboard::barRowHeight(rows.height, static_cast<int>(labels.size()));
  int widest = 0;
  // Measured through the target, so the column follows whatever face the theme
  // gives the small text rather than a font this screen names for itself.
  for (const char* label : labels) {
    widest = std::max<int>(widest, target.measureText(theme.smallText.font, label, theme.smallText).width);
  }
  const int labelWidth = reading_stats::chartLabelColumnWidth(rows.width, widest);
  const uint32_t maxValue = maximum(stats.timeOfDaySeconds);

  for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
    const auto row = stats_dashboard::barRow(rows, index, static_cast<int>(labels.size()), labelWidth, rowHeight);
    fui::TextStyle label = theme.smallText;
    label.align = fui::TextAlign::Left;
    target.text(toFui(row.label), labels[index], label);
    const stats_dashboard::Rect track{row.track.x, row.track.y + 2, row.track.width, std::max(4, rowHeight - 5)};
    target.stroke(toFui(track), fui::Paint::solid(fui::Color::Black), 1);
    const int fill = stats_dashboard::fillFor(track.width, stats.timeOfDaySeconds[index], maxValue);
    if (fill > 0) {
      target.fill(toFui(stats_dashboard::Rect{track.x, track.y, fill, track.height}),
                  fui::Paint::solid(fui::Color::Black));
    }
  }
}

void BookStatsActivity::buildWeekdayChart(UiScreen& screen, const stats_dashboard::Rect& area,
                                          const ReadingStatsData& stats) {
  auto& target = screen.frame().target();
  const auto& theme = screen.theme();
  const int headingHeight = target.lineHeight(theme.bodyText.font);
  fui::TextStyle heading = theme.bodyText;
  heading.align = fui::TextAlign::Left;
  target.text(toFui(stats_dashboard::Rect{area.x, area.y, area.width, headingHeight}), tr(STR_STATS_DAY_OF_WEEK),
              heading);

  const std::array<const char*, 7> labels = {tr(STR_STATS_MON), tr(STR_STATS_TUE), tr(STR_STATS_WED), tr(STR_STATS_THU),
                                             tr(STR_STATS_FRI), tr(STR_STATS_SAT), tr(STR_STATS_SUN)};
  const int labelHeight = target.lineHeight(theme.smallText.font);
  const stats_dashboard::Rect plot{area.x, area.y + headingHeight + 2, area.width,
                                   std::max(8, area.height - headingHeight - 2 - labelHeight)};
  const uint32_t maxValue = maximum(stats.dayOfWeekSeconds);

  for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
    const auto column = stats_dashboard::chartColumn(plot, index, static_cast<int>(labels.size()), plot.height);
    target.stroke(toFui(column), fui::Paint::solid(fui::Color::Black), 1);
    const int fill = stats_dashboard::fillFor(plot.height, stats.dayOfWeekSeconds[index], maxValue);
    if (fill > 0) {
      target.fill(toFui(stats_dashboard::Rect{column.x, column.y + plot.height - fill, column.width, fill}),
                  fui::Paint::solid(fui::Color::Black));
    }
    const auto slot = stats_dashboard::chartColumnSlot(plot, index, static_cast<int>(labels.size()));
    centred(screen, stats_dashboard::Rect{slot.x, plot.y + plot.height + 1, slot.width, labelHeight}, labels[index],
            theme.smallText);
  }
}

void BookStatsActivity::buildDateCards(UiScreen& screen, const stats_dashboard::Rect& area) {
  const auto now = reading_stats::currentLocalDateTime();
  const uint32_t endDay =
      bookStats_.completed
          ? bookStats_.finishedDay
          : (now.valid ? reading_stats::estimateFinishDay(now.dayIndex, bookStats_.startDay,
                                                          bookStats_.totalReadingSeconds, estimatedTimeLeftSeconds_)
                       : 0);
  const int half = area.width / 2;
  card(screen, stats_dashboard::Rect{area.x, area.y, half, area.height}, formatDate(bookStats_.startDay).c_str(),
       tr(STR_STATS_STARTED));
  card(screen, stats_dashboard::Rect{area.x + half, area.y, area.width - half, area.height}, formatDate(endDay).c_str(),
       bookStats_.completed ? tr(STR_STATS_FINISHED) : tr(STR_STATS_EST_FINISH));
}

void BookStatsActivity::buildCurrentBook(UiScreen& screen) {
  auto& target = screen.frame().target();
  const auto& theme = screen.theme();
  const fui::Rect body = screen.body();
  const int lineHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);

  int y = body.y;
  fui::TextStyle titleStyle = theme.bodyText;
  titleStyle.bold = true;
  centred(screen, stats_dashboard::Rect{body.x, y, body.width, lineHeight}, title_.c_str(), titleStyle);
  y += lineHeight + 3;

  // The book's own progress, as a word on the left and a number on the right
  // over the bar they both describe.
  char percent[16];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progressPercent_));
  fui::TextStyle left = theme.smallText;
  left.align = fui::TextAlign::Left;
  fui::TextStyle right = theme.smallText;
  right.align = fui::TextAlign::Right;
  target.text(toFui(stats_dashboard::Rect{body.x, y, body.width, smallHeight}), tr(STR_STATS_PROGRESS), left);
  target.text(toFui(stats_dashboard::Rect{body.x, y, body.width, smallHeight}), percent, right);
  y += smallHeight + 2;

  fui::ProgressBarProps bar;
  bar.value = progressPercent_;
  bar.max = 100;
  bar.border = fui::Paint::solid(fui::Color::Black);
  bar.borderWidth = 1;
  fui::progressBar(screen.frame(), fui::Rect{body.x, static_cast<int16_t>(y), body.width, 10}, bar);
  y += 15;

  const uint32_t averageSession =
      bookStats_.totalSessions == 0 ? 0 : bookStats_.totalReadingSeconds / bookStats_.totalSessions;
  const std::array<MetricRow, 6> values = {{
      {std::to_string(bookStats_.totalSessions), tr(STR_STATS_SESSIONS)},
      {reading_stats::formatDuration(bookStats_.totalReadingSeconds), tr(STR_STATS_READING_TIME)},
      {std::to_string(bookStats_.totalPagesTurned), tr(STR_STATS_PAGES_TURNED)},
      {reading_stats::formatDuration(averageSession), tr(STR_STATS_AVG_SESSION)},
      {formatRate(bookStats_), tr(STR_STATS_PAGES_PER_MIN)},
      {reading_stats::formatDuration(estimatedTimeLeftSeconds_), tr(STR_STATS_TIME_LEFT)},
  }};
  const int bottom = body.y + body.height;
  const int gridHeight = std::min(130, std::max(86, (bottom - y) / 3));
  buildMetricGrid(screen, stats_dashboard::Rect{body.x, y, body.width, gridHeight}, values);
  y += gridHeight + 5;

  if (datesAreUnavailable(bookStats_)) {
    centred(screen, stats_dashboard::Rect{body.x, y + std::max(0, (bottom - y) / 2 - 4), body.width, smallHeight},
            tr(STR_STATS_NO_CLOCK), theme.smallText);
    return;
  }

  const int dateHeight = lineHeight + smallHeight + 5;
  buildDateCards(screen, stats_dashboard::Rect{body.x, y, body.width, dateHeight});
  y += dateHeight + 4;

  const int remaining = bottom - y;
  const int chartHeight = remaining / 2;
  buildTimeOfDayChart(screen, stats_dashboard::Rect{body.x, y, body.width, chartHeight}, bookStats_);
  buildWeekdayChart(screen, stats_dashboard::Rect{body.x, y + chartHeight, body.width, remaining - chartHeight},
                    bookStats_);
}

void BookStatsActivity::buildAllBooks(UiScreen& screen) {
  const auto& theme = screen.theme();
  const fui::Rect body = screen.body();
  const int lineHeight = screen.frame().target().lineHeight(theme.bodyText.font);
  const int smallHeight = screen.frame().target().lineHeight(theme.smallText.font);

  int y = body.y;
  const uint32_t averageSession =
      globalStats_.totalSessions == 0 ? 0 : globalStats_.totalReadingSeconds / globalStats_.totalSessions;
  const std::array<MetricRow, 6> values = {{
      {std::to_string(globalStats_.totalSessions), tr(STR_STATS_SESSIONS)},
      {reading_stats::formatDuration(globalStats_.totalReadingSeconds), tr(STR_STATS_READING_TIME)},
      {std::to_string(globalStats_.totalPagesTurned), tr(STR_STATS_PAGES_TURNED)},
      {reading_stats::formatDuration(averageSession), tr(STR_STATS_AVG_SESSION)},
      {formatRate(globalStats_), tr(STR_STATS_PAGES_PER_MIN)},
      {std::to_string(globalStats_.completedBooks), tr(STR_STATS_COMPLETED)},
  }};
  const int bottom = body.y + body.height;
  const int gridHeight = std::min(150, std::max(92, (bottom - y) / 3));
  buildMetricGrid(screen, stats_dashboard::Rect{body.x, y, body.width, gridHeight}, values);
  y += gridHeight + 4;

  if (datesAreUnavailable(globalStats_)) {
    centred(screen, stats_dashboard::Rect{body.x, y + std::max(0, (bottom - y) / 2 - 4), body.width, smallHeight},
            tr(STR_STATS_NO_CLOCK), theme.smallText);
    return;
  }

  const auto now = reading_stats::currentLocalDateTime();
  const uint32_t today = now.valid ? now.dayIndex : globalStats_.readingHistoryAnchorDay;
  char streak[40];
  snprintf(streak, sizeof(streak), "%s: %u | %s: %u", tr(STR_STATS_STREAK),
           static_cast<unsigned>(globalStats_.currentStreak(today)), tr(STR_STATS_BEST),
           static_cast<unsigned>(globalStats_.longestReadingStreak));
  fui::TextStyle streakStyle = theme.bodyText;
  streakStyle.bold = true;
  centred(screen, stats_dashboard::Rect{body.x, y, body.width, lineHeight}, streak, streakStyle);
  y += lineHeight + 3;

  const int remaining = bottom - y;
  const int chartHeight = remaining / 2;
  buildTimeOfDayChart(screen, stats_dashboard::Rect{body.x, y, body.width, chartHeight}, globalStats_);
  buildWeekdayChart(screen, stats_dashboard::Rect{body.x, y + chartHeight, body.width, remaining - chartHeight},
                    globalStats_);
}

void BookStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), STATS_SIDE_MARGIN,
      static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), STATS_SIDE_MARGIN});
  if (page_ == Page::CurrentBook) {
    buildCurrentBook(screen);
    return;
  }
  buildAllBooks(screen);
}

void BookStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* pageTitle = page_ == Page::CurrentBook ? tr(STR_STATS_CURRENT_BOOK) : tr(STR_STATS_ALL_BOOKS);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, pageTitle);
  renderUi();

  const auto labels = page_ == Page::CurrentBook
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_RESET), "", tr(STR_STATS_MORE))
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_RESET), tr(STR_STATS_BOOK), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
