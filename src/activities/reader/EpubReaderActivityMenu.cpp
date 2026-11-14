#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "KOReaderCredentialStore.h"
#include "QuoteSelectActivity.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetStore.h"
#include "ReaderUtils.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "sleep/SleepPauseToggle.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImage.h"
#include "util/QuoteText.h"
#include "util/SortesSelection.h"

void EpubReaderActivity::openReaderMenu() {
  // Child screens (chapter list, settings) release the section; until the next render
  // rebuilds it, the menu shows the cached position, the same one bookPercent() uses.
  const int currentPage = (section ? section->currentPage : nextPageNumber) + 1;
  const int totalPages = section ? section->estimatedTotalPages() : cachedTotalPages();
  const int bookProgressPercent = bookPercent();
  // Resolve the current chapter name for the menu header, falling back to "Unnamed"
  // when the book has no TOC entry for this spine index.
  std::string chapterName = tr(STR_UNNAMED);
  const int menuTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (menuTocIndex != -1) {
    chapterName = epub->getTocItem(menuTocIndex).title;
  }
  // Settle any favorite rename queued the last time this menu was open, so the row below
  // reads the true state and a rename that failed has already given the reference back.
  // The worker has almost always finished by now; this only applies its outcome.
  DeferredFavorite::reconcile();
  // Wallpaper triage targets the file the last sleep screen actually rendered.
  // Only offer it while that file is still on the card: the user may have deleted
  // or moved it from the file browser since.
  const std::string& lastWallpaper = APP_STATE.lastSleepWallpaperPath;
  const bool hasSleepWallpaper = !lastWallpaper.empty() && Storage.exists(lastWallpaper.c_str());
  const bool wallpaperFavorited = hasSleepWallpaper && FavoriteImage::isFavoritePath(lastWallpaper);
  // A fixed /sleep.pxc or /sleep.bmp has nowhere to be paused to — it is not part
  // of a rotation folder.
  const bool wallpaperPausable = hasSleepWallpaper && crosspoint::sleep::isUnderSleepDirs(lastWallpaper);
  // "View Quotes" is only offered once Grab Quote has actually written a sidecar for
  // this book; an empty viewer would be a dead row.
  const bool hasQuotes = Storage.exists(quote_text::quotesFilePathFor(epub->getPath()).c_str());
  EpubReaderMenuActivity::Context menuContext;
  menuContext.title = epub->getTitle();
  menuContext.author = epub->getAuthor();
  menuContext.chapterName = chapterName;
  menuContext.currentPage = currentPage;
  menuContext.totalPages = totalPages;
  menuContext.bookProgressPercent = bookProgressPercent;
  menuContext.currentOrientation = SETTINGS.orientation;
  menuContext.hasFootnotes = !currentPageFootnotes.empty();
  menuContext.hasPageList = epub->hasPageList();
  menuContext.hasBookmarks = !cachedBookmarks.empty();
  menuContext.hasReaderOverride = prefsCustom_;
  menuContext.paragraphNumbering = prefs_.paragraphNumbering;
  menuContext.paragraphNumberSize = prefs_.paragraphNumberSize;
  menuContext.paragraphNumberPlace = prefs_.paragraphNumberPlace;
  menuContext.paperbackBody = prefs_.paperbackLookBody;
  menuContext.paperbackStatus = prefs_.paperbackLookStatus;
  menuContext.statusBar = prefs_.statusBarEnabled;
  menuContext.progressBar = prefs_.sbOffBar;
  menuContext.hasSleepWallpaper = hasSleepWallpaper;
  menuContext.wallpaperFavorited = wallpaperFavorited;
  menuContext.wallpaperPausable = wallpaperPausable;
  menuContext.hasQuotes = hasQuotes;
  menuContext.hasReturn = !returnHistory.empty();
  menuContext.hasForward = !returnHistory.forwardEmpty();
  startActivityForResult(std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, menuContext),
                         [this](const ActivityResult& result) {
                           // Always apply orientation / paragraph-number / paperback changes even if cancelled
                           const auto& menu = std::get<MenuResult>(result.data);
                           applyOrientation(menu.orientation);
                           applyParagraphNumbering(menu.paragraphNumbering, menu.paragraphNumberSize,
                                                   menu.paragraphNumberPlace);
                           applyPaperbackLook(menu.paperbackBody, menu.paperbackStatus);
                           // Last of the live toggles because it is the only one that repaginates.
                           applyStatusBar(menu.statusBar, menu.progressBar);
                           // A hold inside the menu comes back cancelled with the bound function attached:
                           // no row was chosen, so this replaces the row action rather than following it.
                           //
                           // Runs AFTER the toggles above, never before. Grab Quote hands the picker a raw
                           // Section* and the picker outlives this callback; starting it first and then
                           // letting applyStatusBar/applyOrientation drop the section would leave that
                           // pointer dangling. In this order the worst case is a bound function that
                           // declines because the section is already gone, which every one of them handles.
                           if (menu.holdFunction != CrossPointSettings::LP_MENU_DISABLED) {
                             // Through the manager, like any other binding: the reader takes its own actions
                             // and the rest (Home, Rotate, Sleep...) fall through to the global ones.
                             activityManager.runBoundAction(menu.holdFunction);
                             return;
                           }
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                           }
                         });
}

bool EpubReaderActivity::boundMenuFunctionAvailable(const uint8_t function) const {
  switch (function) {
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials();
    case CrossPointSettings::LP_MENU_DICTIONARY:
      // No dictionary folder configured means the picker would open only to report it.
      return SETTINGS.dictionaryName[0] != '\0' && section != nullptr;
    case CrossPointSettings::LP_MENU_GRAB_QUOTE:
      // The picker is handed this section and outlives the call, so a missing one is fatal.
      return section != nullptr;
    case CrossPointSettings::LP_MENU_GO_TO_PARAGRAPH:
      // The number to type is the one the marks print; with numbering off there is none.
      return prefs_.paragraphNumbering != 0;
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      // Inside a footnote the same binding walks back out, which is what the "Power
      // returns from footnote" toggle governs. With that off, only the way in is offered.
      if (footnoteDepth > 0) return SETTINGS.pwrBtnFootnoteBack != 0;
      return !currentPageFootnotes.empty();
    case CrossPointSettings::LP_MENU_BOOKMARKS:
      // Same test the in-book menu uses for its own Bookmarks row: an empty list screen
      // is a dead press.
      return !cachedBookmarks.empty();
    case CrossPointSettings::LP_MENU_VIEW_QUOTES:
      // Only once Grab Quote has actually written this book's sidecar.
      return Storage.exists(quote_text::quotesFilePathFor(epub->getPath()).c_str());
    // Retired, and never offered again — but a settings file from 0.20.0 could still name
    // one, so they answer "not available" rather than falling into the default below.
    case CrossPointSettings::LP_MENU_SELECT_CHAPTER:
    case CrossPointSettings::LP_MENU_GO_TO_PERCENT:
    case CrossPointSettings::LP_MENU_TEXT_SETTINGS:
      return false;
    case CrossPointSettings::LP_MENU_POPUP:
      // An empty pop-up is a dead press; Pop-up Items has not been filled in yet.
      return SETTINGS.popupItemCount() > 0;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
    case CrossPointSettings::LP_MENU_WALLPAPER_DELETE:
    case CrossPointSettings::LP_MENU_WALLPAPER_FAVORITE:
      // Same test the in-book menu uses to offer its own rows: there must be a wallpaper
      // the lock screen actually showed, and it must still be on the card.
      return !APP_STATE.lastSleepWallpaperPath.empty() && Storage.exists(APP_STATE.lastSleepWallpaperPath.c_str());
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_NEARBY_SEND_BOOK:
    case CrossPointSettings::LP_MENU_READER_SETTINGS:
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
    case CrossPointSettings::LP_MENU_ALIGN_LEFT:
    case CrossPointSettings::LP_MENU_ALIGN_JUSTIFY:
    case CrossPointSettings::LP_MENU_TOGGLE_PARAGRAPH_SPACING:
    // Bound to a key that does not already page (the Home key, or a side key whose single
    // was remapped), paging is an action like any other and the reader can always run it.
    case CrossPointSettings::LP_MENU_PAGE_PREV:
    case CrossPointSettings::LP_MENU_PAGE_NEXT:
      return true;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return false;
  }
}

bool EpubReaderActivity::runBoundMenuFunction(const uint8_t function) {
  // Every caller treats false as "the press was not consumed", so refusing here is what
  // keeps a bound-but-impossible action from swallowing the button.
  if (!boundMenuFunctionAvailable(function)) return false;

  switch (function) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
      if (showBookmarkMessage) return false;
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return true;
    case CrossPointSettings::LP_MENU_KOSYNC:
      // False when sync cannot run (no credentials stored); the caller then leaves the
      // Confirm release alone so the normal reader menu still opens.
      return launchKOReaderSync();
    case CrossPointSettings::LP_MENU_DICTIONARY:
      if (showDictionaryMessage) return false;
      openDictionaryWordSelect();
      return true;
    case CrossPointSettings::LP_MENU_GRAB_QUOTE:
      openQuoteGrab();
      return true;
    // The rest reuse the in-book menu's own entry points rather than repeating them, so a
    // binding and the matching menu row can never drift apart.
    case CrossPointSettings::LP_MENU_BOOKMARKS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::BOOKMARKS);
      return true;
    case CrossPointSettings::LP_MENU_VIEW_QUOTES:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::VIEW_QUOTES);
      return true;
    case CrossPointSettings::LP_MENU_NEARBY_SEND_BOOK:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::NEARBY_SEND_BOOK);
      return true;
    case CrossPointSettings::LP_MENU_GO_TO_PARAGRAPH:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::GO_TO_PARAGRAPH);
      return true;
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      if (footnoteDepth > 0) {
        if (SETTINGS.pwrBtnFootnoteBack == 0) return false;
        restoreSavedPosition();
        return true;
      }
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::FOOTNOTES);
      return true;
    case CrossPointSettings::LP_MENU_READER_SETTINGS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READER_SETTINGS);
      return true;
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
      // Not a menu row: the menu toggles this live and reports it through MenuResult, so
      // there is no MenuAction to reuse. sbOffBar is passed unchanged — this flips the
      // master switch only, leaving the hidden-bar progress choice alone.
      applyStatusBar(prefs_.statusBarEnabled ? 0 : 1, prefs_.sbOffBar);
      // applyStatusBar only drops the section for relayout; the menu path repaints when
      // the menu closes over it, and a binding has no menu to close. Without this the bar
      // appeared or vanished only at the next page turn, which read as a dead button.
      requestUpdate();
      return true;
    case CrossPointSettings::LP_MENU_ALIGN_LEFT:
    case CrossPointSettings::LP_MENU_ALIGN_JUSTIFY: {
      const uint8_t alignment = function == CrossPointSettings::LP_MENU_ALIGN_LEFT ? CrossPointSettings::LEFT_ALIGN
                                                                                   : CrossPointSettings::JUSTIFIED;
      // Already there: still consumed, so the press is not handed on as a dead one.
      if (prefs_.paragraphAlignment == alignment) return true;
      // Per-book, like Text Size in the same panel.
      prefs_.paragraphAlignment = alignment;
      prefsCustom_ = true;
      writeReaderOverride(prefs_);
      reloadForReaderPrefsChange();
      requestUpdate();
      return true;
    }
    case CrossPointSettings::LP_MENU_TOGGLE_PARAGRAPH_SPACING:
      // Per-book, like alignment beside it.
      prefs_.extraParagraphSpacing = prefs_.extraParagraphSpacing ? 0 : 1;
      prefsCustom_ = true;
      writeReaderOverride(prefs_);
      reloadForReaderPrefsChange();
      requestUpdate();
      return true;
    case CrossPointSettings::LP_MENU_POPUP:
      openQuickMenu();
      return true;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::WALLPAPER_HOLD);
      return true;
    case CrossPointSettings::LP_MENU_WALLPAPER_DELETE:
      // Asks for confirmation itself before removing the file; see the menu action.
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::WALLPAPER_DELETE);
      return true;
    case CrossPointSettings::LP_MENU_WALLPAPER_FAVORITE:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::WALLPAPER_FAVORITE);
      return true;
    case CrossPointSettings::LP_MENU_PAGE_PREV:
      pageTurn(false);
      return true;
    case CrossPointSettings::LP_MENU_PAGE_NEXT:
      pageTurn(true);
      return true;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return false;
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  // A router only: each action group lives in its own member below, so this stays a table
  // of which row runs what.
  if (action == EpubReaderMenuActivity::MenuAction::DELETE_CACHE ||
      action == EpubReaderMenuActivity::MenuAction::DELETE_BOOK ||
      action == EpubReaderMenuActivity::MenuAction::REMOVE_FROM_RECENTS) {
    if (blockSortesAction()) return;
  }
  switch (action) {
    // Navigation
    case EpubReaderMenuActivity::MenuAction::RETURN:
    case EpubReaderMenuActivity::MenuAction::FORWARD: {
      RenderLock lock(*this);
      std::optional<ReaderReturnHistory::Position> here;
      if (currentPageSpineIndex >= 0 && currentPageVisibleOffset) {
        here = ReaderReturnHistory::Position{currentPageSpineIndex, *currentPageVisibleOffset};
      }
      const auto target = action == EpubReaderMenuActivity::MenuAction::RETURN ? returnHistory.beginReturn(here)
                                                                               : returnHistory.beginForward(here);
      if (target) jumpToContentOffset(target->spineIndex, target->contentOffset);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER:
      openChapterSelection();
      break;
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES:
      openFootnotes();
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PAGE:
      openPageEntry();
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT:
      openPercentSelection();
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PARAGRAPH:
      openParagraphEntry();
      break;
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS:
      openBookmarks();
      break;
    case EpubReaderMenuActivity::MenuAction::SEARCH:
      openSearch();
      break;
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK:
      addBookmark();
      break;
    // Reading tools
    case EpubReaderMenuActivity::MenuAction::DICTIONARY:
      openDictionaryWordSelect();
      break;
    case EpubReaderMenuActivity::MenuAction::DICTIONARY_HISTORY:
      openDictionaryHistory();
      break;
    case EpubReaderMenuActivity::MenuAction::GRAB_QUOTE:
      openQuoteGrab();
      break;
    case EpubReaderMenuActivity::MenuAction::VIEW_QUOTES:
      openQuotesViewer();
      break;
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR:
      openPageQr();
      break;
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    // Lock-screen wallpaper
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_FAVORITE:
      toggleWallpaperFavorite();
      break;
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_HOLD:
      toggleWallpaperHold();
      break;
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_PAUSE:
      toggleWallpaperPause();
      break;
    case EpubReaderMenuActivity::MenuAction::WALLPAPER_DELETE:
      confirmWallpaperDelete();
      break;
    // This book's look
    case EpubReaderMenuActivity::MenuAction::STEAL_LOOK:
      openStealLook();
      break;
    case EpubReaderMenuActivity::MenuAction::READING_THEMES:
      openReadingThemes();
      break;
    case EpubReaderMenuActivity::MenuAction::READER_SETTINGS:
      openReaderSettings();
      break;
    case EpubReaderMenuActivity::MenuAction::CUSTOMISE_STATUS_BAR:
      openStatusBarSettings();
      break;
    case EpubReaderMenuActivity::MenuAction::RESET_READER_SETTINGS:
      confirmResetReaderSettings();
      break;
    // Sync and transfer
    case EpubReaderMenuActivity::MenuAction::SYNC:
      launchKOReaderSync();
      break;
    case EpubReaderMenuActivity::MenuAction::NEARBY_SYNC:
      launchNearbyPositionSync();
      break;
    case EpubReaderMenuActivity::MenuAction::NEARBY_SEND_BOOK:
      launchNearbyBookSend();
      break;
    // The book file (each leaves the reader)
    case EpubReaderMenuActivity::MenuAction::REMOVE_FROM_RECENTS:
      removeFromRecents();
      break;
    case EpubReaderMenuActivity::MenuAction::DELETE_BOOK:
      confirmDeleteBook();
      break;
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE:
      deleteCacheAndGoHome();
      break;
  }
}
