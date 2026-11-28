#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "BookSearchResultsActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "TxtReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/BookFilingNames.h"
#include "util/BookSearch.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

void TxtReaderActivity::openSettingsPopup() {
  const std::vector<std::string> rows = {tr(STR_SEARCH_IN_BOOK), tr(STR_DELETE_BOOK)};
  settingsPopup.show(StrId::STR_BOOK_MENU, rows, 0, [this](const int index) {
    if (index == 0) {
      openBookSearch();
    } else {
      askDeleteBook();
    }
  });
  requestUpdate();
}

void TxtReaderActivity::openBookSearch() {
  if (!txt) return;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_SEARCH_IN_BOOK)),
                                              lastSearchQuery, book_search::MAX_QUERY_BYTES, InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled || !txt) {
          requestUpdate();
          return;
        }
        const auto* kr = std::get_if<KeyboardResult>(&result.data);
        char norm[book_search::MAX_QUERY_BYTES + 1];
        if (!kr || book_search::normalizeQuery(kr->text, norm) == 0) {
          requestUpdate();
          return;
        }
        lastSearchQuery = kr->text;

        auto scanner = makeUniqueNoThrow<book_search::TxtStreamScanner>(lastSearchQuery, book_search::DEFAULT_MAX_HITS);
        auto chunk = makeUniqueNoThrow<char[]>(1024);
        if (!scanner || !chunk) {
          LOG_ERR("SRCH", "OOM allocating TXT search buffers");
          requestUpdate();
          return;
        }

        GUI.drawPopup(renderer, tr(STR_SEARCHING_BOOK));
        const uint32_t startMs = millis();
        size_t bytesScanned = 0;
        {
          HalFile file;
          if (Storage.openFileForRead("SRCH", txt->getPath(), file)) {
            uint32_t ticks = 0;
            while (!scanner->isFull()) {
              const int n = file.read(chunk.get(), 1024);
              if (n <= 0) break;
              scanner->feed(chunk.get(), static_cast<size_t>(n));
              bytesScanned += static_cast<size_t>(n);
              if ((++ticks & 0x07) == 0) {
                busy::tick();
                resetTaskWatchdogIfSubscribed();
              }
            }
          }
        }
        scanner->finish();
        const uint32_t elapsedMs = millis() - startMs;
        const bool truncated = scanner->truncated() || scanner->isFull();
        auto rawHits = scanner->takeHits();
        scanner.reset();
        chunk.reset();

        LOG_INF("SRCH", "TXT search '%s': %u hits%s, %u B in %u ms", lastSearchQuery.c_str(),
                static_cast<unsigned>(rawHits.size()), truncated ? "+" : "", static_cast<unsigned>(bytesScanned),
                static_cast<unsigned>(elapsedMs));

        std::vector<BookSearchResultsActivity::ResultItem> items;
        items.reserve(rawHits.size());
        for (const auto& h : rawHits) {
          int page = 0;
          if (!pageOffsets.empty()) {
            const auto it = std::upper_bound(pageOffsets.begin(), pageOffsets.end(), static_cast<size_t>(h.offset));
            if (it != pageOffsets.begin()) {
              page = static_cast<int>(std::distance(pageOffsets.begin(), it) - 1);
            }
          }
          char pageBuf[20];
          snprintf(pageBuf, sizeof(pageBuf), "p.%d", page + 1);
          items.push_back({page, h.offset, pageBuf, h.snippet});
        }

        auto resultsAct = makeUniqueNoThrow<BookSearchResultsActivity>(renderer, mappedInput, lastSearchQuery,
                                                                       std::move(items), truncated);
        if (!resultsAct) {
          LOG_ERR("SRCH", "OOM allocating BookSearchResultsActivity");
          requestUpdate();
          return;
        }
        startActivityForResult(std::move(resultsAct), [this](const ActivityResult& res) {
          if (!res.isCancelled && !pageOffsets.empty()) {
            const auto& jump = std::get<ProgressChangeResult>(res.data);
            currentPage = std::clamp(jump.spineIndex, 0, std::max(0, totalPages - 1));
          }
          pagesUntilFullRefresh = 0;
          requestUpdate();
        });
      });
}

void TxtReaderActivity::relayoutForFontChange() {
  if (!txt) return;
  // Page numbers mean nothing across a font change, but byte offsets do: remember the
  // byte the reader is showing, then land on whichever new page contains it.
  const size_t anchorOffset = pageOffsets.empty() ? 0 : pageOffsets[currentPage];

  initialized = false;
  pageOffsets.clear();
  currentPageLines.clear();
  initializeReader();

  // loadProgress() inside initializeReader restores the page number saved for the old
  // font, so the anchor is applied after it, not before.
  int landing = 0;
  for (size_t i = 0; i < pageOffsets.size(); i++) {
    if (pageOffsets[i] > anchorOffset) break;
    landing = static_cast<int>(i);
  }
  currentPage = landing;
  requestUpdate();
}

bool TxtReaderActivity::runBoundAction(const uint8_t function) {
  // The pop-up owns the buttons while it is up; a bound gesture must not act underneath it.
  if (settingsPopup.isActive()) return true;

  if (function == CrossPointSettings::LP_MENU_PAGE_PREV || function == CrossPointSettings::LP_MENU_PAGE_NEXT) {
    pageTurn(function == CrossPointSettings::LP_MENU_PAGE_NEXT);
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_NEXT_CHAPTER || function == CrossPointSettings::LP_MENU_PREV_CHAPTER) {
    skipPages(function == CrossPointSettings::LP_MENU_NEXT_CHAPTER ? 10 : -10);
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_OPEN_READER_MENU) {
    openSettingsPopup();  // what Confirm opens here
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_ALIGN_LEFT || function == CrossPointSettings::LP_MENU_ALIGN_JUSTIFY) {
    const uint8_t alignment = function == CrossPointSettings::LP_MENU_ALIGN_LEFT ? CrossPointSettings::LEFT_ALIGN
                                                                                 : CrossPointSettings::JUSTIFIED;
    if (SETTINGS.paragraphAlignment == alignment) return true;
    // TXT has no per-book prefs: the global setting is the one it reads.
    SETTINGS.paragraphAlignment = alignment;
    SETTINGS.saveToFile();
    quietIndex_ = true;       // straight back to the page, no Indexing popup
    relayoutForFontChange();  // re-reads the alignment and keeps the byte on screen
    return true;
  }

  switch (simple_reader_shortcut::resolve(function, /*supportsStatusBarToggle=*/true)) {
    case simple_reader_shortcut::Action::ToggleStatusBar:
      SETTINGS.sbEnabled = SETTINGS.sbEnabled ? 0 : 1;
      SETTINGS.saveToFile();
      // The bar's band overlaps the reading margin, so showing or hiding it changes the
      // viewport and therefore how many lines fit a page. Re-index rather than repaint,
      // and keep the reader on the byte it was showing (the font-change path already
      // does exactly this).
      relayoutForFontChange();
      return true;
    case simple_reader_shortcut::Action::WallpaperHold:
      ReaderUtils::toggleWallpaperHold(renderer, pagesUntilFullRefresh);
      requestUpdate();
      return true;
    case simple_reader_shortcut::Action::None:
      break;
  }
  // Not runnable in this reader, or runnable only when a card read says so (Hold
  // Wallpaper checks the file is still there). Fall through to the shared handler, which
  // answers the device-wide actions; anything else is a dead press.
  return false;
}

void TxtReaderActivity::askDeleteBook() {
  if (!txt) return;
  const std::string path = txt->getPath();
  // The file name, deliberately, where the EPUB prompt uses the book's title: a TXT
  // "title" is only the file name with its extension removed, so naming the file is both
  // the same information and the more precise answer to "which file is about to go".
  const std::string name = std::string(bookfiling::fileNameOf(path));
  // Deleting a book file is not undoable, so it asks first, the same as deleting a
  // sleep wallpaper does.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK) + std::string("?"), name),
      [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate();
          return;
        }
        pendingDeleteBook = true;
        onGoHome();
      });
}
