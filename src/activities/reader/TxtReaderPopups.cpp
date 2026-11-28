#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "TxtReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookFilingNames.h"

void TxtReaderActivity::openSettingsPopup() {
  // One row: a TXT book follows the EPUB font and size, so Delete Book is all the pop-up holds.
  const std::vector<std::string> rows = {tr(STR_DELETE_BOOK)};
  settingsPopup.show(StrId::STR_BOOK_MENU, rows, 0, [this](const int) { askDeleteBook(); });
  requestUpdate();
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

  if (function == CrossPointSettings::LP_MENU_ALIGN_LEFT || function == CrossPointSettings::LP_MENU_ALIGN_JUSTIFY ||
      function == CrossPointSettings::LP_MENU_TOGGLE_ALIGNMENT) {
    const uint8_t alignment =
        function == CrossPointSettings::LP_MENU_TOGGLE_ALIGNMENT
            ? (SETTINGS.paragraphAlignment == CrossPointSettings::LEFT_ALIGN ? CrossPointSettings::JUSTIFIED
                                                                             : CrossPointSettings::LEFT_ALIGN)
            : (function == CrossPointSettings::LP_MENU_ALIGN_LEFT ? CrossPointSettings::LEFT_ALIGN
                                                                  : CrossPointSettings::JUSTIFIED);
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
