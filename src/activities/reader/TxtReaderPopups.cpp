#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookFilingNames.h"

void TxtReaderActivity::openSettingsPopup() {
  // Rows are fixed, so the indices below are stable and can be compared directly.
  const std::vector<std::string> rows = {tr(STR_FONT), tr(STR_SIZE), tr(STR_DELETE_BOOK)};
  settingsPopup.show(StrId::STR_BOOK_MENU, rows, 0, [this](const int index) {
    switch (index) {
      case 0:
        openFontPopup();
        break;
      case 1:
        openSizePopup();
        break;
      default:
        askDeleteBook();
        break;
    }
  });
  requestUpdate();
}

void TxtReaderActivity::openFontPopup() {
  // Entry 0 is the built-in family; the rest are whatever is installed on the card.
  const auto& families = sdFontSystem.registry().getFamilies();
  std::vector<std::string> options;
  options.reserve(families.size() + 1);
  options.push_back(tr(STR_BUILT_IN_FONT));
  for (const auto& family : families) {
    options.push_back(family.name);
  }

  int current = 0;
  if (SETTINGS.txtSdFontFamilyName[0] != '\0') {
    const int idx = sdFontSystem.registry().getFamilyIndex(SETTINGS.txtSdFontFamilyName);
    if (idx >= 0) current = idx + 1;
  }

  settingsPopup.show(StrId::STR_FONT, options, current, [this, options](const int index) {
    if (index == 0) {
      SETTINGS.txtSdFontFamilyName[0] = '\0';
    } else {
      strncpy(SETTINGS.txtSdFontFamilyName, options[index].c_str(), sizeof(SETTINGS.txtSdFontFamilyName) - 1);
      SETTINGS.txtSdFontFamilyName[sizeof(SETTINGS.txtSdFontFamilyName) - 1] = '\0';
    }
    // A family ships only the sizes it ships, so the point size is snapped into the
    // new family's set before anything tries to resolve a font id from the pair.
    const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.txtSdFontFamilyName);
    SETTINGS.txtFontPointSize = snapToNearestPointSize(sizes, SETTINGS.txtFontPointSize);
    SETTINGS.saveToFile();
    sdFontSystem.ensureLoadedFor(renderer, SETTINGS.txtSdFontFamilyName, SETTINGS.txtFontPointSize);
    relayoutForFontChange();
  });
  requestUpdate();
}

void TxtReaderActivity::openSizePopup() {
  const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.txtSdFontFamilyName);
  std::vector<std::string> options;
  options.reserve(sizes.size());
  int current = 0;
  for (size_t i = 0; i < sizes.size(); i++) {
    options.push_back(std::to_string(sizes[i]));
    if (sizes[i] == SETTINGS.txtFontPointSize) current = static_cast<int>(i);
  }

  settingsPopup.show(StrId::STR_SIZE, options, current, [this, sizes](const int index) {
    if (index < 0 || index >= static_cast<int>(sizes.size())) return;
    if (sizes[index] == SETTINGS.txtFontPointSize) {
      requestUpdate();
      return;
    }
    SETTINGS.txtFontPointSize = sizes[index];
    SETTINGS.saveToFile();
    sdFontSystem.ensureLoadedFor(renderer, SETTINGS.txtSdFontFamilyName, SETTINGS.txtFontPointSize);
    relayoutForFontChange();
  });
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
