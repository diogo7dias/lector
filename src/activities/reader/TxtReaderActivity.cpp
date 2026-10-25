#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <Epub/PageVerticalAlignment.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookFilingNames.h"
#include "util/BookProgressFile.h"
#include "util/ReadingJournalStore.h"
#include "util/ReadingPercent.h"

void TxtReaderActivity::onEnter() {
  Activity::onEnter();

  if (!txt) {
    return;
  }

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Only one SD font size is resident at a time, and it is the EPUB reader's unless
  // this is called; without it a TXT-only SD family silently lays out in the built-in
  // font. A family that has left the card falls back to built-in and persists nothing.
  sdFontSystem.ensureLoadedFor(renderer, SETTINGS.txtSdFontFamilyName, SETTINGS.txtFontPointSize);

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "");
  journal.clock.touch(millis());

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  if (txt && progressSaveDebouncer.hasPending()) {
    saveProgress(progressSaveDebouncer.lastObservedPosition());
  }
  if (txt) reading_journal::append(journal, millis(), txt->getTitle().c_str());

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Persist reading progress % to the recents store so the home in-progress list shows a
  // [NN%] badge for TXT books (EPUB writes this on exit too; comics/XTC intentionally do not).
  // setProgress skips the SD write when unchanged, so this is cheap on every exit.
  if (txt && totalPages > 0 && !pendingDeleteBook) {
    const int pct = reading_percent::pagePercent(currentPage, totalPages);
    RECENT_BOOKS.setProgress(txt->getPath(), pct);
    // Same number again, beside the book's cache, for the file browser's row badge and its
    // last-read order. The enclosing condition already excludes a book being deleted.
    book_progress::Marker marker;
    marker.percent = static_cast<uint8_t>(pct);
    marker.readOrder = ++APP_STATE.readOrderCounter;
    book_progress::write(txt->getCachePath(), marker);
    APP_STATE.saveToFile();
  }

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;

  // Deleting the book the reader is holding happens here, after the Txt (and any open
  // handle on the file) is released — removing a file with a handle still open is what
  // the EPUB reader's move-on-exit filing avoids in the same way.
  const std::string deletePath = pendingDeleteBook && txt ? txt->getPath() : std::string();
  txt.reset();
  if (!deletePath.empty()) {
    if (Storage.remove(deletePath.c_str())) {
      // The cache directory is keyed on the path, so leaving it behind would strand
      // an index and a progress file that nothing will ever open again.
      clearBookCache(deletePath);
      RECENT_BOOKS.removeByPath(deletePath);
      // Without this, Back on the home screen would try to reopen the deleted file.
      if (APP_STATE.openEpubPath == deletePath) {
        APP_STATE.openEpubPath.clear();
      }
    } else {
      LOG_ERR("TRS", "Failed to delete book: %s", deletePath.c_str());
    }
  }

  APP_STATE.saveToFile();
}

void TxtReaderActivity::loop() {
  // The popup owns every button while it is up, including Back (which closes it), so
  // it is handled before the reader's own Back and page-turn handling. Before the latch
  // below, as in EpubReaderActivity: a press the pop-up eats must not be latched, or
  // its release reads as a genuine Back on the page and leaves the book.
  if (settingsPopup.handleInput(mappedInput, [this]() { requestUpdate(); })) {
    return;
  }

  // See ReaderUtils::ButtonPressLatch: swallow a Back release whose press belonged to a
  // child screen that closed on press, instead of reading it as "leave the book".
  backLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Back));

  // Touch on the open page, read once per loop, as in EpubReaderActivity: a tap or swipe
  // turns the page, and the menu gesture opens what Confirm opens here.
  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  if (ReaderUtils::isTouchMenuGesture(renderer, mappedInput, touch.menu) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSettingsPopup();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, txt ? txt->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<TxtReaderActivity*>(ctx)->onGoHome(); }},
                                        backLatch_)) {
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  pageTurn(nextTriggered);
}

void TxtReaderActivity::pageTurn(const bool forward) {
  if (!forward) {
    if (currentPage == 0) return;
    journal.turn(millis(), false);
    currentPage--;
    requestUpdate();
    return;
  }
  if (currentPage < totalPages - 1) {
    journal.turn(millis(), true);
    currentPage++;
    if (currentPage == totalPages - 1) journal.finished = true;
    requestUpdate();
  } else {
    onGoHome();
  }
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  // TXT has its own font selection, independent of the EPUB reader's.
  cachedFontId = SETTINGS.getTxtReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Uniform margins use screenMargin on every side; otherwise top/bottom are
  // independent while screenMargin stays the horizontal margin. Changing any of
  // these alters the viewport (and linesPerPage), which self-invalidates the page
  // cache, so no extra cache-key field is needed beyond cachedScreenMargin.
  const uint8_t topMargin = SETTINGS.screenMarginTop;
  const uint8_t bottomMargin = SETTINGS.screenMarginBottom;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  // v2 status bar: reserve a band at the top and/or bottom edge (whichever holds
  // items), overlapping the reading margin (max, not sum) like the old bottom bar.
  // TXT has no chapters, so chapter-only items are hidden. A greedy (truncate-off)
  // book-source title wraps, needing extra band height.
  const StatusBarBlock sb = SETTINGS.statusBar();
  // TXT has no chapter title: the slot holds the book title (see TitleSlot).
  bool sbTitleTop = false;
  const int sbTitleLines =
      UITheme::getStatusBarV2TitleLines(sb, renderer, false, "", txt->getTitle().c_str(), &sbTitleTop);
  const int sbTitleExtraPx = (sbTitleLines - 1) * renderer.getLineHeight(UI_10_FONT_ID);
  const int sbTop = UITheme::getInstance().getStatusBarV2TopHeight(sb, false, sbTitleTop ? sbTitleExtraPx : 0);
  const int sbBottom = UITheme::getInstance().getStatusBarV2BottomHeight(sb, false, sbTitleTop ? 0 : sbTitleExtraPx);
  cachedOrientedMarginTop += std::max<int>(topMargin, sbTop);
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom += std::max<int>(bottomMargin, sbBottom);

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  detectEncoding();

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex();
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, 300,
                              unsupportedEncoding ? tr(STR_UNSUPPORTED_ENCODING) : tr(STR_EMPTY_FILE), true,
                              EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  // The popup paints over the page that renderPage just put on the panel. That costs
  // one extra panel update, but only while the popup is actually open.
  settingsPopup.processRender(renderer, mappedInput);

  // Save progress
  if (progressSaveDebouncer.observe(currentPage)) saveProgress(currentPage);
}

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int verticalOffset = fullTextPageOffset(
      viewportHeight, 0, (static_cast<int>(currentPageLines.size()) - 1) * lineHeight, lineHeight, lineHeight);

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop + verticalOffset;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            // For plain text, justified is treated as left-aligned
            // (true justification would require word spacing adjustments)
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass — text accumulated, no drawing
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering. Paperback Look (body, global) thickens the page glyphs; reset
  // before the status bar (own flag) and the grayscale AA pass below.
  renderer.setPaperbackLook(SETTINGS.paperbackLookBody);
  renderLines();
  renderer.setPaperbackLook(false);
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    renderer.setTextContrast(SETTINGS.textContrast);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
    renderer.setTextContrast(0);
  }
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  // v2 bar. TXT is chapterless: the page item shows BOOK pages (chapterPage/Pages
  // hold book page/total), chapter-only items hide, and the title (book source)
  // can wrap. The band was reserved in the inset calc above.
  StatusBarData d;
  d.hasChapters = false;
  d.chapterPage = static_cast<int>(currentPage) + 1;
  d.chapterPages = static_cast<int>(totalPages);
  d.bookPercent = reading_percent::pagePercent(currentPage, totalPages);
  d.bookTitle = txt->getTitle();
  // Paperback Look (status bar): thicken only status-bar glyphs, then reset.
  renderer.setPaperbackLook(SETTINGS.paperbackLookStatus);
  GUI.drawStatusBarV2(renderer, d, SETTINGS.statusBar());
  renderer.setPaperbackLook(false);
}

void TxtReaderActivity::saveProgress(const uint32_t page) {
  uint8_t data[4];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %u", static_cast<unsigned>(page));
    return;
  }
  progressSaveDebouncer.markPersisted(page);
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (ProgressFile::openForRead("TRS", txt->getCachePath(), f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = reading_percent::pagePercent(currentPage, totalPages);
  return info;
}
