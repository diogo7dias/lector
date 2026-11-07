#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderSearchActivity.h"
#include "MappedInputManager.h"
#include "QrDisplayActivity.h"
#include "QuoteSelectActivity.h"
#include "QuotesViewerActivity.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "SdCardFontSystem.h"
#include "StealLookActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "sleep/SleepPauseToggle.h"
#include "util/BoundMenuLabels.h"
#include "util/FavoriteImage.h"
#include "util/QuoteText.h"
#include "util/ReadingPercent.h"

void EpubReaderActivity::openDictionaryWordSelect(const int anchorX, const int anchorY) {
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  // Word geometry must match render(): the same margins and status bar bands.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  pageMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // The page was laid out in this book's font (per-book prefs), so the word boxes must be too.
  startActivityForResult(std::make_unique<DictionaryWordSelectActivity>(
                             renderer, mappedInput, std::move(page), orientedMarginLeft, orientedMarginTop,
                             SETTINGS.getReaderFontId(prefs_), anchorX, anchorY),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderActivity::openQuoteGrab() {
  if (!section) return;

  // Word geometry must match render(): the same margins and status bar bands.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  pageMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // Lay out the picker with this book's actual reader font (per-book prefs), so
  // the highlight boxes line up with the rendered glyphs.
  const int readerFontId = SETTINGS.getReaderFontId(prefs_);
  startActivityForResult(
      // The picker loads its own pages: a quote may run past this one, and it turns
      // pages itself while the reader stays suspended and its section stays alive.
      std::make_unique<QuoteSelectActivity>(renderer, mappedInput, section.get(), section->currentPage,
                                            orientedMarginLeft, orientedMarginTop, *epub, currentSpineIndex,
                                            readerFontId),
      [this](const ActivityResult&) {
        loadQuoteAnchors();  // a fresh grab appended to the sidecar
        requestUpdate();
      });
}

void EpubReaderActivity::openQuickMenu() {
  const auto& functions = CrossPointSettings::POPUP_ITEM_FUNCTIONS;

  std::vector<std::string> labels;
  std::vector<bool> disabledRows;
  labels.reserve(CrossPointSettings::POPUP_ITEM_MAX);
  disabledRows.reserve(CrossPointSettings::POPUP_ITEM_MAX);
  quickMenuFunctions.clear();
  quickMenuFunctions.reserve(CrossPointSettings::POPUP_ITEM_MAX);

  for (const uint8_t function : functions) {
    if (!SETTINGS.isPopupItem(function)) continue;
    const bool available = boundMenuFunctionAvailable(function);
    // Fixed-width status column, and the pop-up is left-aligned, so ticking or losing a
    // footnote never shifts a label sideways.
    // One row, two names: a wallpaper already starred offers Unfavorite. Same swap the
    // reader menu's own Sleep Screen row makes, so the two never disagree.
    const StrId label = function == CrossPointSettings::LP_MENU_WALLPAPER_FAVORITE &&
                                FavoriteImage::isFavoritePath(APP_STATE.lastSleepWallpaperPath)
                            ? StrId::STR_UNFAVORITE_WALLPAPER
                            : boundMenuActionLabel(function);
    labels.push_back(std::string(available ? "    " : "[X] ") + I18N.get(label));
    disabledRows.push_back(!available);
    quickMenuFunctions.push_back(function);
  }

  if (quickMenuFunctions.empty()) return;

  quickMenu.showWithDisabled(StrId::STR_QUICK_MENU, labels, disabledRows, 0, true, [this](const int index) {
    if (index < 0 || index >= static_cast<int>(quickMenuFunctions.size())) return;
    const uint8_t function = quickMenuFunctions[index];
    // The pop-up has already closed itself by the time this runs, so the action draws
    // over a page the pop-up no longer owns.
    if (!runBoundMenuFunction(function)) {
      GUI.drawPopup(renderer, tr(STR_NOT_AVAILABLE));
      scheduleGhostCleanup();
    }
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderActivity::openFootnotes() {
  startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                             navigateToHref(footnoteResult.href, true);
                           }
                           requestUpdate();
                         });
}

void EpubReaderActivity::openChapterSelection() {
  int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  // Release the section while the chapter list is up (mirrors the
  // READER_SETTINGS path): picking a chapter resets it anyway, and its
  // tens-of-KB footprint is the difference between the chapter list
  // holding its CJK glyph arena (RAM-only repaints) and re-reading
  // glyphs from SD on every row step. Cancel restores via the same
  // cached-position rebuild a settings edit uses.
  {
    RenderLock lock(*this);
    // Highlight the sub-chapter on screen, not just the file's first entry.
    if (section) tocIndex = section->getTocIndexForPage(section->currentPage);
    cacheSectionPosition();
    section.reset();
  }
  startActivityForResult(std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, *epub, tocIndex),
                         [this](const ActivityResult& result) {
                           RenderLock lock(*this);
                           if (!result.isCancelled) {
                             const auto& chapterResult = std::get<ChapterResult>(result.data);
                             if (chapterResult.spineIndex < 0 || chapterResult.spineIndex >= epub->getSpineItemsCount())
                               return;
                           }
                           recordJumpOrigin(!result.isCancelled);
                           if (!result.isCancelled) {
                             const auto& chapterResult = std::get<ChapterResult>(result.data);
                             clearDeferredReposition();
                             currentSpineIndex = chapterResult.spineIndex;

                             // If anchor is not empty, it will be used later to calculate the page number.
                             pendingAnchor = chapterResult.anchor;

                             // Otherwise page 0 will be used.
                             nextPageNumber = 0;

                             section.reset();
                           }
                         });
}

void EpubReaderActivity::buildChapterMarks() {
  // Each TOC read is a seek on the SD, so the walk is capped: a TOC this long is a
  // reference book whose top-level marks would not fit the bar anyway.
  constexpr int kMaxTocScan = 512;
  const int tocCount = std::min(epub->getTocItemsCount(), kMaxTocScan);
  int count = 0;
  for (int i = 0; i < tocCount && count >= 0; i++) {
    const auto entry = epub->getTocItem(i);
    if (entry.level != 1 || entry.spineIndex < 0) continue;
    count = statusbar::addChapterMark(chapterMarks_, count,
                                      static_cast<int>(epub->calculateProgress(entry.spineIndex, 0.0f) * 1000.0f));
  }
  chapterMarkCount_ = count > 0 ? count : 0;
}

// The cached count describes cachedSpineIndex; after a chapter jump it is the old
// chapter's, so it reads as unknown (0) until the new section is built.
int EpubReaderActivity::cachedTotalPages() const {
  return cachedSpineIndex == currentSpineIndex ? cachedChapterTotalPageCount : 0;
}

int EpubReaderActivity::bookPercent() const {
  if (!epub) return 0;
  // The whole-book page count is unknown, so the chapter's page share is weighted by its
  // bytes. estimatedTotalPages(), not the live pageCount: mid-build that is a watermark.
  const int page = section ? section->currentPage : nextPageNumber;
  const int total = section ? section->estimatedTotalPages() : cachedTotalPages();
  return reading_percent::toPercent(
      epub->calculateProgress(currentSpineIndex, reading_percent::pageFraction(page, total)));
}

void EpubReaderActivity::openPercentSelection() {
  const int initialPercent = bookPercent();
  startActivityForResult(std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             jumpToPercent(std::get<PercentResult>(result.data).percent);
                           }
                         });
}

void EpubReaderActivity::openPageEntry() {
  // Labels are what the printed book says, "147" or "xii", so the field takes text.
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_GO_TO_PAGE)),
                                                                 std::string(), 16, InputType::Text),
                         [this](const ActivityResult& result) {
                           const auto* kr = std::get_if<KeyboardResult>(&result.data);
                           const std::string href =
                               !result.isCancelled && kr ? epub->findPrintedPage(kr->text) : std::string();
                           if (href.empty()) {
                             if (!result.isCancelled) LOG_DBG("ERS", "No printed page: %s", kr ? kr->text.c_str() : "");
                             requestUpdate();
                             return;
                           }
                           recordJumpOrigin(true);
                           navigateToHref(href);
                         });
}

void EpubReaderActivity::openParagraphEntry() {
  // Ask for a paragraph number, then jump to it (the number shown by the marks).
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_GO_TO_PARAGRAPH)),
                                              std::string(), 6, InputType::Text),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
            const int n = atoi(kr->text.c_str());
            if (n >= 1) {
              jumpToParagraph(n);
              return;
            }
          }
        }
        requestUpdate();
      });
}

void EpubReaderActivity::openDictionaryHistory() {
  // Same guard Look Up uses: with no dictionary set, every row in the list would open
  // only to report that, so say it once here instead.
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  startActivityForResult(std::make_unique<DictionaryHistoryActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderActivity::openQuotesViewer() {
  // The row only appears when the sidecar exists, so the viewer opens straight
  // onto the file Grab Quote wrote.
  startActivityForResult(
      std::make_unique<QuotesViewerActivity>(renderer, mappedInput, quote_text::quotesFilePathFor(epub->getPath())),
      [this](const ActivityResult&) {
        loadQuoteAnchors();  // a delete in there rewrites the sidecar
        requestUpdate();
      });
}

void EpubReaderActivity::openPageQr() {
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    std::string fullText = section->getTextFromSectionFile();
    if (!fullText.empty()) {
      startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                             [this](const ActivityResult& result) {});
      return;
    }
  }
  // If no text or page loading failed, just close menu
  requestUpdate();
}

void EpubReaderActivity::openStealLook() {
  // Pick another book that has a custom look and copy its reader settings onto
  // this one. A one-time snapshot, not a link: changing that book later does not
  // change this one. A cancelled pick changes nothing.
  startActivityForResult(makeUniqueNoThrow<StealLookActivity>(renderer, mappedInput, epub->getPath()),
                         [this](const ActivityResult& res) {
                           if (res.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           if (const auto* fp = std::get_if<FilePathResult>(&res.data)) {
                             applyStolenLook(fp->path);
                           } else {
                             requestUpdate();
                           }
                         });
}

void EpubReaderActivity::openReadingThemes() {
  // The themes screen edits the store itself; only "Apply" comes back, as the index
  // of the theme to adopt. Cancelling, renaming or deleting changes nothing here.
  startActivityForResult(makeUniqueNoThrow<ReaderPresetsActivity>(renderer, mappedInput, prefs_),
                         [this](const ActivityResult& res) {
                           const auto* pr = res.isCancelled ? nullptr : std::get_if<PresetResult>(&res.data);
                           const ReaderPreset* preset = pr ? READER_PRESETS.get(pr->index) : nullptr;
                           if (!preset) {
                             requestUpdate();
                             return;
                           }
                           ReaderPrefs stored = preset->prefs;
                           reader_preset_migration::keepStatusBarWhenAbsent(preset->carriesStatusBar, prefs_, stored);
                           if (!sortesMode) setGlobalBarLayout(statusBarOf(stored));
                           const ReaderPrefs adopted = applyReaderPrefsFrom(stored);
                           // A theme naming an SD font that has left the card comes back
                           // corrected. Write the correction to the card so the theme is
                           // repaired once, instead of falling back on every apply.
                           if (std::memcmp(&adopted, &stored, sizeof(ReaderPrefs)) != 0) {
                             READER_PRESETS.overwrite(pr->index, adopted);
                           }
                         });
}

void EpubReaderActivity::openReaderSettings() {
  // The screen edits a copy of this book's look; each change lands on the card through
  // the sink, and the result callback relays the book out once at the end. Leaving by
  // Home or sleep skips that callback, but the screen commits on its way out and this
  // activity is still alive then (the top of the stack exits first).
  readerEdit_ = prefs_;
  startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(), prefs_,
                                                                &EpubReaderActivity::readerEditSinkThunk, this),
                         [this](const ActivityResult&) { applyReaderSettingsEdit(); });
}

void EpubReaderActivity::openStatusBarSettings() {
  // The screen edits a copy of this book's bar; each change lands on the card (the
  // global bar, and this book's sidecar if it has one) through the sink, and the result
  // callback relays the book out once at the end.
  statusBarEdit_ = statusBarOf(prefs_);
  startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput, statusBarEdit_,
                                                                     &EpubReaderActivity::statusBarEditSinkThunk, this),
                         [this](const ActivityResult&) { applyStatusBarEdit(); });
}

void EpubReaderActivity::openBookmarks() {
  startActivityForResult(std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, *epub, epub->getPath()),
                         [this](const ActivityResult& result) { onBookmarkJumpResult(result); });
}

void EpubReaderActivity::openSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_SEARCH)), lastSearchQuery,
                                              SearchMatcher::MAX_QUERY, InputType::Text),
      [this](const ActivityResult& result) {
        const auto* kr = result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
        if (!kr || kr->text.empty()) {
          requestUpdate();
          return;
        }
        lastSearchQuery = kr->text;
        // A picked hit comes back like a bookmark with an exact offset, and lands the same way.
        startActivityForResult(std::make_unique<EpubReaderSearchActivity>(renderer, mappedInput, *epub, kr->text),
                               [this](const ActivityResult& found) { onBookmarkJumpResult(found); });
      });
}
