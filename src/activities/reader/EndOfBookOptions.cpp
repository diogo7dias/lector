#include "EndOfBookOptions.h"

#include <ClockZones.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

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
#include "util/BookProgressFile.h"
#include "util/BusyTick.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"
#include "util/QuoteText.h"

namespace {
// Display name without the file extension, mirroring the file browser rows
std::string displayName(const std::string& filename) {
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

// Queries LibraryIndexFile for up to MAX_SUGGESTIONS unread books matching the same author
// (preferred) or filename series prefix. Streams records in small stack batches so RAM use
// stays tiny regardless of library size.
void findFromLibraryIndex(const std::string& currentBookPath, const std::string& bookAuthor,
                          std::vector<EndOfBookOptions::Suggestion>& out) {
  library::LibraryIndexFile index;
  if (!index.open(library::libraryIndexPath())) return;
  const uint16_t total = index.bookCount();
  if (total == 0) return;

  const bool hasAuthor = !end_of_book::trimAscii(bookAuthor).empty();
  const bool hasPrefix = end_of_book::extractSeriesPrefix(currentBookPath).size() >= end_of_book::MIN_PREFIX_LEN;
  if (!hasAuthor && !hasPrefix) return;

  std::vector<end_of_book::LibraryCandidate> picks;
  picks.reserve(EndOfBookOptions::MAX_SUGGESTIONS + 1);

  // 4 x 32 B = 128 B on stack (well below the 256 B stack limit).
  constexpr size_t BATCH_SIZE = 4;
  library::ClixRecord batch[BATCH_SIZE];
  std::string candPath;
  std::string candAuthor;
  std::string candTitle;

  for (uint16_t ordinal = 0; ordinal < total;) {
    const size_t n = index.readRecords(ordinal, BATCH_SIZE, batch);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i, ++ordinal) {
      if ((ordinal & 0x0F) == 0) busy::tick();
      const auto& rec = batch[i];
      if (!index.readPath(rec, candPath)) continue;
      if (end_of_book::shouldSkipLibraryEntry(currentBookPath, candPath)) continue;

      bool sameAuthor = false;
      if (hasAuthor && rec.authorLen > 0 && index.readAuthor(rec, candAuthor)) {
        sameAuthor = end_of_book::isSameAuthor(bookAuthor, candAuthor);
      }
      const bool samePrefix = !sameAuthor && hasPrefix && end_of_book::matchesFilenamePrefix(currentBookPath, candPath);
      if (!sameAuthor && !samePrefix) continue;

      book_progress::Marker marker;
      if (book_progress::readForBook(candPath, marker) && marker.percent >= 100) continue;
      if (!Storage.exists(candPath.c_str())) continue;

      std::string label;
      if (rec.titleLen > 0 && index.readTitle(rec, candTitle) && !end_of_book::trimAscii(candTitle).empty()) {
        label = std::move(candTitle);
      } else {
        label = displayName(std::string(bookfiling::fileNameOf(candPath)));
      }
      end_of_book::insertCandidate(picks, {std::move(label), candPath, sameAuthor ? uint8_t{0} : uint8_t{1}},
                                   EndOfBookOptions::MAX_SUGGESTIONS);
    }
  }

  out.reserve(picks.size());
  for (auto& pick : picks) {
    out.push_back({std::move(pick.displayName), std::move(pick.fullPath)});
  }
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
                                const std::string& bookAuthor, const bool canSync, const bool allowQuotes) {
  if (isLoaded.load(std::memory_order_acquire)) {
    return;
  }

  suggestions.clear();
  const bool inRecents = bookfiling::isInFolder(currentBookPath, bookfiling::RECENTS_FOLDER);
  if (!inRecents) {
    const std::string folder = FsHelpers::extractFolderPath(currentBookPath);
    const auto siblingNames = NextBookFinder::findNextBooks(currentBookPath, MAX_SUGGESTIONS);
    suggestions.reserve(siblingNames.size());
    for (const auto& name : siblingNames) {
      suggestions.push_back({displayName(name), folder == "/" ? "/" + name : folder + "/" + name});
    }
  }
  if (end_of_book::shouldUseLibraryFallback(currentBookPath, suggestions.size())) {
    findFromLibraryIndex(currentBookPath, bookAuthor, suggestions);
  }

  const bool hasQuotes = allowQuotes && Storage.exists(quote_text::quotesFilePathFor(currentBookPath).c_str());
  rows = end_of_book::buildActionRows(static_cast<int>(suggestions.size()), canSync, hasQuotes);
  selector = 0;

  bookTitle = title;
  author = bookAuthor;
  quote = allowQuotes ? lastQuote(currentBookPath) : std::string();
  datesLine[0] = '\0';
  char finished[12];
  if (formatToday(finished, sizeof(finished)))
    snprintf(datesLine, sizeof(datesLine), tr(STR_EOB_FINISHED_ON), finished);

  // Release-publish so the main task, which gates all access on isLoaded, never
  // observes a partially built list
  isLoaded.store(true, std::memory_order_release);
}

bool EndOfBookOptions::menuActive() const { return isLoaded.load(std::memory_order_acquire) && !rows.empty(); }

EndOfBookOptions::Action EndOfBookOptions::activateRow(const int rowIndex, std::string* openPath) const {
  if (rowIndex < 0 || rowIndex >= rows.count) return Action::None;
  const auto& row = rows[rowIndex];
  switch (row.kind) {
    case end_of_book::RowKind::Book:
      if (row.bookIndex >= 0 && row.bookIndex < static_cast<int>(suggestions.size())) {
        if (openPath) *openPath = suggestions[static_cast<size_t>(row.bookIndex)].fullPath;
        return Action::OpenBook;
      }
      return Action::None;
    case end_of_book::RowKind::SyncProgress:
      return Action::SyncProgress;
    case end_of_book::RowKind::ViewQuotes:
      return Action::ViewQuotes;
    case end_of_book::RowKind::Home:
      return Action::GoHome;
  }
  return Action::None;
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  // A tap on a row answers, the way Confirm does.
  int tappedRow = 0;
  if (input.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow < rows.count) {
    selector = tappedRow;
    return activateRow(selector, openPath);
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    return activateRow(selector, openPath);
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
  if (triggered(MappedInputManager::Button::NavPrevious)) {
    selector = ButtonNavigator::previousIndex(selector, rows.count);  // wraps to the bottom
    return Action::Redraw;
  }
  if (triggered(MappedInputManager::Button::NavNext)) {
    selector = ButtonNavigator::nextIndex(selector, rows.count);  // wraps to the top
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

  // Always draw inside the hint-safe area with action rows (Sync Progress when
  // configured, View Quotes when present, Home) and button hints, even when no
  // next-book suggestions were found.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int titleY = safe.y + safe.height / 8;
  UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, titleY, tr(STR_END_OF_BOOK), true, EpdFontFamily::REGULAR);
  const int finishedBottom = drawFinished(renderer, safe, titleY + lineH);
  int listTop = finishedBottom + metrics.verticalSpacing * 2;
  if (!suggestions.empty()) {
    UITheme::drawCenteredText(renderer, safe, UI_10_FONT_ID, listTop, tr(STR_EOB_CONTINUE_WITH));
    listTop += lineH + metrics.verticalSpacing * 2;
  }
  const int listHeight = safe.y + safe.height - listTop - metrics.verticalSpacing;
  // Titles wrap over the lines they need rather than being cut; the visible range
  // comes back from the draw and feeds the scroll offset, as in the file browser.
  const ListVisibility vis =
      GUI.drawWrappedList(renderer, Rect{safe.x, listTop, safe.width, listHeight}, rows.count, selector, scrollOffset,
                          [this](const int index) -> std::string {
                            if (index < 0 || index >= rows.count) return {};
                            const auto& row = rows[index];
                            switch (row.kind) {
                              case end_of_book::RowKind::Book:
                                return suggestions[static_cast<size_t>(row.bookIndex)].displayName;
                              case end_of_book::RowKind::SyncProgress:
                                return tr(STR_SYNC_PROGRESS);
                              case end_of_book::RowKind::ViewQuotes:
                                return tr(STR_VIEW_QUOTES);
                              case end_of_book::RowKind::Home:
                                return tr(STR_EOB_HOME);
                            }
                            return {};
                          });
  scrollOffset = vis.firstVisible;

  const auto labels = input.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
