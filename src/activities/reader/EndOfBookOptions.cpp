#include "EndOfBookOptions.h"

#include <ClockZones.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryGroups.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"
// ReaderUtils.h pulls in ActivityManager.h, which only forward-declares Activity while holding
// std::unique_ptr<Activity> members. Destroying that unique_ptr needs the complete type, so the
// definition must be visible here.
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"
#include "util/QuoteText.h"

namespace {
// Display name without folder prefix or file extension, mirroring the file browser rows
std::string displayName(const std::string& filename) {
  const auto slash = filename.rfind('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const auto dot = filename.rfind('.');
  if (dot != std::string::npos && dot > start) return filename.substr(start, dot - start);
  return filename.substr(start);
}

void prependSeriesNext(const std::string& currentBookPath, const std::string& folder, std::vector<std::string>& names,
                       const size_t maxSuggestions) {
  library::LibraryIndexFile index;
  std::string nextPath;
  if (!index.open(library::libraryIndexPath()) || !library::findNextInSeries(index, currentBookPath, nextPath) ||
      !Storage.exists(nextPath.c_str())) {
    return;
  }
  // Remove duplicate if NextBookFinder already listed the same book as a sibling filename.
  names.erase(std::remove_if(names.begin(), names.end(),
                             [&](const std::string& item) {
                               if (!item.empty() && item[0] == '/') return item == nextPath;
                               const std::string p = folder == "/" ? "/" + item : folder + "/" + item;
                               return p == nextPath;
                             }),
              names.end());
  names.insert(names.begin(), std::move(nextPath));
  if (names.size() > maxSuggestions) names.resize(maxSuggestions);
}

// The last quote saved from this book, from the tail of its sidecar only: a record longer
// than the tail is skipped rather than shown cut at its start.
std::string lastQuote(const std::string& bookPath) {
  constexpr size_t TAIL = 1024;
  const std::string path = quote_text::quotesFilePathFor(bookPath);
  if (!Storage.exists(path.c_str())) return {};
  HalFile f;
  if (!Storage.openFileForRead("EOB", path, f)) return {};
  const size_t size = f.fileSize();
  const size_t n = std::min(size, TAIL);
  if (n == 0 || !f.seekSet(size - n)) return {};
  std::string buf(n, '\0');
  if (f.read(&buf[0], n) != static_cast<int>(n)) return {};

  // Records end in "\n---": drop the last one's, then the one before it marks where it starts.
  size_t end = buf.size();
  while (end > 0 && quote_text::isRecordGap(buf[end - 1])) --end;
  if (end >= 4 && buf.compare(end - 4, 4, "\n---") == 0) end -= 4;
  const size_t prev = end > 0 ? buf.rfind("\n---", end - 1) : std::string::npos;
  if (prev == std::string::npos && n < size) return {};
  const size_t start = prev == std::string::npos ? 0 : prev + 4;
  if (start >= end) return {};
  buf = buf.substr(start, end - start);

  size_t pos = 0;
  quote_text::QuoteRecord record;
  if (!quote_text::nextQuoteRecord(buf, pos, record) || record.textEnd <= record.textStart) return {};
  return buf.substr(record.textStart, record.textEnd - record.textStart);
}

// Today's local date as YYYY-MM-DD, in the zone the clock settings name. False when the
// clock was never set.
bool formatToday(char* out, const size_t size) {
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return false;
  const int64_t utc = clock_zones::utcSeconds(year, month, day, hour, minute);
  const time_t local =
      static_cast<time_t>(utc + clock_zones::offsetMinutes(SETTINGS.clockTimezone, SETTINGS.clockDst, utc) * 60);
  tm t{};
  gmtime_r(&local, &t);
  snprintf(out, size, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return true;
}
}  // namespace

void EndOfBookOptions::loadOnce(const std::string& currentBookPath, const std::string& title,
                                const std::string& bookAuthor) {
  if (isLoaded.load(std::memory_order_acquire)) {
    return;
  }
  folder = FsHelpers::extractFolderPath(currentBookPath);
  names = NextBookFinder::findNextBooks(currentBookPath, MAX_SUGGESTIONS);
  prependSeriesNext(currentBookPath, folder, names, MAX_SUGGESTIONS);
  selector = 0;

  bookTitle = title;
  author = bookAuthor;
  quote = lastQuote(currentBookPath);
  datesLine[0] = '\0';
  char finished[12];
  if (formatToday(finished, sizeof(finished)))
    snprintf(datesLine, sizeof(datesLine), tr(STR_EOB_FINISHED_ON), finished);

  // Release-publish so the main task, which gates all access on isLoaded, never
  // observes a partially built list
  isLoaded.store(true, std::memory_order_release);
}

bool EndOfBookOptions::menuActive() const { return isLoaded.load(std::memory_order_acquire) && !names.empty(); }

std::string EndOfBookOptions::fullPath(const size_t index) const {
  if (index >= names.size()) {
    return {};
  }
  if (!names[index].empty() && names[index][0] == '/') return names[index];
  return folder == "/" ? "/" + names[index] : folder + "/" + names[index];
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  // A tap on a row answers, the way Confirm does. The row list is the suggestions followed
  // by the Home entry, so the tapped index maps straight onto the selector.
  int tappedRow = 0;
  if (input.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow <= static_cast<int>(names.size())) {
    selector = tappedRow;
    if (selector < static_cast<int>(names.size())) {
      if (openPath) *openPath = fullPath(selector);
      return Action::OpenBook;
    }
    return Action::GoHome;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selector < static_cast<int>(names.size())) {
      if (openPath) {
        *openPath = fullPath(selector);
      }
      return Action::OpenBook;
    }
    return Action::GoHome;  // "Home" entry selected
  }

  // Short-press Back returns to the last page; a long press falls through to the
  // reader's own handler (file browser). Home is reached through the list's Home entry.
  if (input.wasReleased(MappedInputManager::Button::Back) && input.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    return Action::LastPage;
  }

  // Selection movement on the standard list navigation buttons (side Up/Down plus front
  // Left/Right, orientation swap included), on the press like every other list. The
  // press that turned the final page already fired in the reader; its release cannot
  // double-fire here, because this screen arrives through a transition that arms the
  // input gate.
  const auto triggered = [&](const MappedInputManager::Button button) { return input.wasPressed(button); };
  const int itemCount = static_cast<int>(names.size()) + 1;  // + "Home" entry
  if (triggered(MappedInputManager::Button::NavPrevious)) {
    selector = ButtonNavigator::previousIndex(selector, itemCount);  // wraps to the bottom
    return Action::Redraw;
  }
  if (triggered(MappedInputManager::Button::NavNext)) {
    selector = ButtonNavigator::nextIndex(selector, itemCount);  // wraps to the top
    return Action::Redraw;
  }
  return Action::None;
}

int EndOfBookOptions::drawFinished(const GfxRenderer& renderer, const Rect& area, int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int width = area.width - 2 * metrics.contentSidePadding;
  // Every line wraps inside the padding: a longer translation must not run off the panel.
  const auto lines = [&](const char* text, const int maxLines,
                         const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    if (!text[0]) return;
    for (const auto& l : renderer.wrappedText(UI_10_FONT_ID, text, width, maxLines, style)) {
      UITheme::drawCenteredText(renderer, area, UI_10_FONT_ID, y, l.c_str(), true, style);
      y += lineH;
    }
  };

  y += metrics.verticalSpacing;
  lines(bookTitle.c_str(), 2, EpdFontFamily::BOLD);
  lines(author.c_str(), 1);
  y += metrics.verticalSpacing;
  lines(datesLine, 1);
  if (!quote.empty()) {
    std::string shown = "\"" + quote + "\"";
    std::replace(shown.begin(), shown.end(), '\n', ' ');
    y += metrics.verticalSpacing;
    lines(shown.c_str(), 3);
  }
  return y;
}

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  if (!menuActive()) {
    // No suggestions: the title at 3/8 of the screen height, as it always stood, and the
    // book's lines under it.
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, false, false);
    const int titleY = renderer.getScreenHeight() * 3 / 8;
    UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, titleY, tr(STR_END_OF_BOOK), true, EpdFontFamily::REGULAR);
    drawFinished(renderer, safe, titleY + lineH);
    return;
  }

  // Suggestion menu: title, the book's lines, list (+ Home entry) and button hints. The
  // hints are drawn at the physical front buttons, which is a logical side/top edge in the
  // rotated orientations — lay out inside the safe area so nothing hides behind them.
  // Vertical positions derive from the safe-area height and font line heights so other
  // panel resolutions scale (review request on #2532).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int titleY = safe.y + safe.height / 8;
  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, titleY, tr(STR_END_OF_BOOK), true, EpdFontFamily::REGULAR);
  const int subtitleY = drawFinished(renderer, safe, titleY + lineH) + metrics.verticalSpacing * 2;
  const int listTop = subtitleY + lineH + metrics.verticalSpacing * 2;
  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, subtitleY, tr(STR_EOB_CONTINUE_WITH));
  const int listHeight = safe.y + safe.height - listTop - metrics.verticalSpacing;
  // Titles wrap over the lines they need rather than being cut; the visible range
  // comes back from the draw and feeds the scroll offset, as in the file browser.
  const ListVisibility vis = GUI.drawWrappedList(
      renderer, Rect{safe.x, listTop, safe.width, listHeight}, static_cast<int>(names.size()) + 1, selector,
      scrollOffset, [this](const int index) {
        return index < static_cast<int>(names.size()) ? displayName(names[index]) : std::string(tr(STR_EOB_HOME));
      });
  scrollOffset = vis.firstVisible;

  const auto labels = input.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
