#include "StatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingJournalStore.h"

namespace {

constexpr StrId MONTHS[] = {StrId::STR_MONTH_JANUARY, StrId::STR_MONTH_FEBRUARY, StrId::STR_MONTH_MARCH,
                            StrId::STR_MONTH_APRIL,   StrId::STR_MONTH_MAY,      StrId::STR_MONTH_JUNE,
                            StrId::STR_MONTH_JULY,    StrId::STR_MONTH_AUGUST,   StrId::STR_MONTH_SEPTEMBER,
                            StrId::STR_MONTH_OCTOBER, StrId::STR_MONTH_NOVEMBER, StrId::STR_MONTH_DECEMBER};

void totalsText(char* buf, const size_t size, const reading_journal::Totals& t) {
  char duration[16];
  reading_journal::formatDuration(duration, sizeof(duration), t.seconds);
  snprintf(buf, size, tr(STR_STATS_TIME_PAGES), duration, static_cast<unsigned long>(t.pages));
}

}  // namespace

void StatsActivity::onEnter() {
  haveJournal = reading_journal::summarize(summary) && summary.all.sessions > 0;
  UiStatusActivity::onEnter();
}

UiStatusActivity::StatusView StatsActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_STATS);
  if (!haveJournal) view.lines[0] = tr(STR_STATS_EMPTY);
  return view;
}

int StatsActivity::drawMonth(const int x, const int y, const int width, const int maxHeight) const {
  const int year = summary.today.year;
  const int month = summary.today.month;
  const int today = summary.today.day;
  const int days = reading_journal::daysInMonth(year, month);
  // 1970-01-01 was a Thursday, so the Monday-first column is (days + 3) % 7.
  const int64_t first = clock_zones::daysFromCivil(year, month, 1);
  const int lead = static_cast<int>(((first + 3) % 7 + 7) % 7);
  const int rows = (lead + days + 6) / 7;

  // Title row: the month on the left, days read so far on the right.
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, x, y, I18N.get(MONTHS[month - 1]), true, EpdFontFamily::BOLD);
  int read = 0;
  uint16_t most = 1;
  for (int d = 0; d < today; ++d) read += summary.monthMinutes[d] > 0;
  for (int d = 0; d < days; ++d) most = std::max(most, summary.monthMinutes[d]);
  char daysText[32];
  snprintf(daysText, sizeof(daysText), tr(STR_STATS_DAYS_OF), read, today);
  const int smallH = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + width - renderer.getTextWidth(UI_10_FONT_ID, daysText), y + titleH - smallH,
                    daysText, true);

  constexpr int gap = 6;
  const int cellW = (width - 6 * gap) / 7;
  const int wdY = y + titleH + gap * 2;
  const char* letters = tr(STR_STATS_WEEKDAYS);
  for (int c = 0; c < 7 && letters[c]; ++c) {
    const char letter[2] = {letters[c], '\0'};
    const int lx =
        x + c * (cellW + gap) + (cellW - renderer.getTextWidth(UI_10_FONT_ID, letter, EpdFontFamily::BOLD)) / 2;
    renderer.drawText(UI_10_FONT_ID, lx, wdY, letter, true, EpdFontFamily::BOLD);
  }

  const int gridY = wdY + smallH + gap;
  const int cellH = std::max(smallH + 6, std::min(cellW, (y + maxHeight - gridY) / rows - gap));
  for (int d = 1; d <= days; ++d) {
    const int slot = lead + d - 1;
    const int cx = x + (slot % 7) * (cellW + gap);
    const int cy = gridY + (slot / 7) * (cellH + gap);
    // A glass, filled from the bottom by this day's share of the month's longest day.
    const int fill = summary.monthMinutes[d - 1] * (cellH - 4) / most;
    if (d <= today) renderer.drawRect(cx, cy, cellW, cellH, d == today ? 4 : 2, true);
    if (fill > 0) renderer.fillRect(cx + 2, cy + cellH - 2 - fill, cellW - 4, fill, true);

    char num[4];
    snprintf(num, sizeof(num), "%d", d);
    const int ty = cy + (cellH - smallH) / 2;
    // White once the fill reaches the middle of the digits, black over paper.
    const bool onInk = cy + cellH - 2 - fill <= ty + smallH / 2;
    renderer.drawText(UI_10_FONT_ID, cx + (cellW - renderer.getTextWidth(UI_10_FONT_ID, num)) / 2, ty, num, !onInk);
  }
  return gridY + rows * (cellH + gap) - y;
}

void StatsActivity::drawRows(const int x, int y, const int width) const {
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const auto row = [&](const char* label, const char* value) {
    renderer.drawText(UI_12_FONT_ID, x, y, label, true);
    renderer.drawText(UI_12_FONT_ID, x + width - renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD), y,
                      value, true, EpdFontFamily::BOLD);
    y += lineH + 10;
  };
  char text[48];
  if (summary.haveToday) {
    totalsText(text, sizeof(text), summary.week);
    row(tr(STR_STATS_LAST_7_DAYS), text);
    totalsText(text, sizeof(text), summary.year);
    row(tr(STR_STATS_THIS_YEAR), text);
  }
  totalsText(text, sizeof(text), summary.all);
  row(tr(STR_STATS_ALL_TIME), text);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(summary.all.finished));
  row(tr(STR_STATS_FINISHED), text);
  if (!summary.haveToday) renderer.drawText(UI_10_FONT_ID, x, y + 6, tr(STR_STATS_NO_CLOCK), true);
}

// The base drew the header and the hints; the body is ours. Portrait stacks the month
// over the totals, landscape puts them side by side.
bool StatsActivity::drawOverlay() {
  if (!haveJournal) return false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int pad = metrics.contentSidePadding;
  const int top = metrics.topPadding + BaseTheme::headerHeightFor(renderer, w, tr(STR_STATS)) + metrics.verticalSpacing;
  const int bottom = h - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (!summary.haveToday) {
    drawRows(pad, top, w - 2 * pad);
    return false;
  }
  if (w > h) {
    const int half = (w - 3 * pad) / 2;
    drawMonth(pad, top, half, bottom - top);
    drawRows(2 * pad + half, top + renderer.getLineHeight(UI_12_FONT_ID) * 2, half);
    return false;
  }
  const int used = drawMonth(pad, top, w - 2 * pad, (bottom - top) * 3 / 5);
  const int ruleY = top + used + metrics.verticalSpacing;
  renderer.fillRect(pad, ruleY, w - 2 * pad, 2, true);
  drawRows(pad, ruleY + metrics.verticalSpacing * 2, w - 2 * pad);
  return false;
}
