#include "StatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingJournalStore.h"

namespace {

void totalsLine(char* buf, const size_t size, const reading_journal::Totals& t) {
  char duration[16];
  reading_journal::formatDuration(duration, sizeof(duration), t.seconds);
  snprintf(buf, size, tr(STR_STATS_TIME_PAGES), duration, static_cast<unsigned long>(t.pages));
}

}  // namespace

void StatsActivity::onEnter() {
  haveJournal = reading_journal::summarize(summary) && summary.all.sessions > 0;
  totalsLine(weekLine, sizeof(weekLine), summary.week);
  totalsLine(yearLine, sizeof(yearLine), summary.year);
  totalsLine(allLine, sizeof(allLine), summary.all);
  snprintf(yearFinished, sizeof(yearFinished), tr(STR_STATS_FINISHED),
           static_cast<unsigned long>(summary.year.finished));
  snprintf(allFinished, sizeof(allFinished), tr(STR_STATS_FINISHED), static_cast<unsigned long>(summary.all.finished));
  if (summary.haveToday) {
    int read = 0;
    for (int d = 0; d < summary.today.day; ++d) read += summary.monthMinutes[d] > 0;
    snprintf(daysLine, sizeof(daysLine), tr(STR_STATS_DAYS_READ), read, summary.today.day);
  }
  UiStatusActivity::onEnter();
}

UiStatusActivity::StatusView StatsActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_STATS);
  if (!haveJournal) {
    view.lines[0] = tr(STR_STATS_EMPTY);
    return view;
  }
  size_t s = 0;
  if (summary.haveToday) {
    view.sections[s].heading = tr(STR_STATS_LAST_7_DAYS);
    view.sections[s].lines[0] = weekLine;
    view.sections[s++].lines[1] = daysLine;
    view.sections[s].heading = tr(STR_STATS_THIS_YEAR);
    view.sections[s].lines[0] = yearLine;
    view.sections[s++].lines[1] = yearFinished;
  }
  view.sections[s].heading = tr(STR_STATS_ALL_TIME);
  view.sections[s].lines[0] = allLine;
  view.sections[s].lines[1] = allFinished;
  if (!summary.haveToday) view.sections[s].lines[2] = tr(STR_STATS_NO_CLOCK);
  return view;
}

// This month as a Monday-first calendar: a filled square is a day with reading, the
// thick outline is today. Bottom of the body in portrait; the right half in landscape,
// where the text above leaves no room under it.
bool StatsActivity::drawOverlay() {
  if (!haveJournal || !summary.haveToday) return false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const bool landscape = w > h;

  const int year = summary.today.year;
  const int month = summary.today.month;
  const int days = reading_journal::daysInMonth(year, month);
  // 1970-01-01 was a Thursday, so Monday-first column = (days + 3) % 7.
  const int64_t first = clock_zones::daysFromCivil(year, month, 1);
  const int lead = static_cast<int>(((first + 3) % 7 + 7) % 7);
  const int rows = (lead + days + 6) / 7;

  const int areaW = landscape ? w / 2 - metrics.contentSidePadding : w - 2 * metrics.contentSidePadding;
  const int cell = std::min(40, areaW / 7);
  const int gridW = cell * 7;
  const int gridX = landscape ? w - metrics.contentSidePadding - gridW : (w - gridW) / 2;
  const int bottom = h - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int gridY = landscape ? std::max(0, (h - rows * cell) / 2) : bottom - rows * cell;

  const int textH = renderer.getLineHeight(UI_10_FONT_ID);
  for (int d = 1; d <= days; ++d) {
    const int slot = lead + d - 1;
    const int x = gridX + (slot % 7) * cell;
    const int y = gridY + (slot / 7) * cell;
    const bool read = summary.monthMinutes[d - 1] > 0;
    const bool today = d == summary.today.day;
    constexpr int inset = 2;
    if (read) renderer.fillRect(x + inset, y + inset, cell - 2 * inset, cell - 2 * inset, true);
    if (today) {
      renderer.drawRect(x + inset, y + inset, cell - 2 * inset, cell - 2 * inset, 3, true);
    } else if (!read && d < summary.today.day) {
      renderer.drawRect(x + inset, y + inset, cell - 2 * inset, cell - 2 * inset, 1, true);
    }
    char num[4];
    snprintf(num, sizeof(num), "%d", d);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, num);
    renderer.drawText(UI_10_FONT_ID, x + (cell - tw) / 2, y + (cell - textH) / 2, num, !read);
  }
  return false;
}
