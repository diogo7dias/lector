#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
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
#include "reading_stats/ReadingStatsClock.h"
#include "util/BookCacheUtils.h"
#include "util/BookFilingNames.h"
#include "util/BookProgressFile.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 4;          // Increment when cache format changes (4: form-feed page breaks)
}  // namespace

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

  // Reading stats. Latched at open so a mid-book toggle cannot half-track a
  // session; the cache dir must exist first because this book's stats file
  // lives inside it.
  statsTrackingActive = SETTINGS.readingStatsEnabled != 0;
  if (statsTrackingActive) {
    statsSession.configure({.idleThresholdSeconds = SETTINGS.readingStatsIdleSeconds(),
                            .minimumPageSeconds = 2,
                            .minimumSessionSeconds = 60});
    statsSession.begin(txt->getCachePath(), reading_stats::currentLocalDateTime());
  }

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  if (statsTrackingActive) {
    statsSession.pause(millis());
    if (!statsSession.finish()) LOG_ERR("RSTAT", "Failed to save TXT reading stats");
  }

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Persist reading progress % to the recents store so the home in-progress list shows a
  // [NN%] badge for TXT books (EPUB writes this on exit too; comics/XTC intentionally do not).
  // setProgress skips the SD write when unchanged, so this is cheap on every exit.
  if (txt && totalPages > 0 && !pendingDeleteBook) {
    int pct = static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f);
    if (pct > 100) pct = 100;
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
  // See ReaderUtils::ButtonPressLatch: swallow a Back release whose press belonged to a
  // child screen that closed on press, instead of reading it as "leave the book".
  backLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Back));

  // The popup owns every button while it is up, including Back (which closes it), so
  // it is handled before the reader's own Back and page-turn handling.
  if (settingsPopup.handleInput(mappedInput, [this]() { requestUpdate(); })) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSettingsPopup();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, txt ? txt->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<TxtReaderActivity*>(ctx)->onGoHome(); }},
                                        backLatch_)) {
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    // Backward is re-reading, not progress: close the page out rather than credit it.
    if (statsTrackingActive) statsSession.pause(millis());
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      if (statsTrackingActive) {
        statsSession.forwardTurn(millis());
        if (currentPage + 1 == totalPages - 1) {
          const auto now = reading_stats::currentLocalDateTime();
          statsSession.markCompleted(now.valid ? now.dayIndex : 0);
        }
      }
      currentPage++;
      requestUpdate();
    } else {
      onGoHome();
    }
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
  int sbTitleExtraPx = 0;
  if (SETTINGS.statusBarEnabled() && SETTINGS.sbTitlePos != CrossPointSettings::SB_ANCHOR_OFF &&
      SETTINGS.sbTitleTruncate == 0) {
    // TXT resolves any title source to the book title (no chapters to fall back from).
    const int lines = UITheme::getStatusBarV2TitleLines(renderer, txt->getTitle().c_str());
    sbTitleExtraPx = (lines - 1) * renderer.getLineHeight(UI_10_FONT_ID);
  }
  const bool sbTitleTop = SETTINGS.sbTitlePos >= CrossPointSettings::SB_ANCHOR_TL &&
                          SETTINGS.sbTitlePos <= CrossPointSettings::SB_ANCHOR_TR;
  const int sbTop = UITheme::getInstance().getStatusBarV2TopHeight(false, sbTitleTop ? sbTitleExtraPx : 0);
  const int sbBottom = UITheme::getInstance().getStatusBarV2BottomHeight(false, sbTitleTop ? 0 : sbTitleExtraPx);
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

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // Prime the SD card font's advance table with this chunk's codepoints.
  // Without this, every getTextAdvanceX() call in the wrap loop below triggers
  // on-demand glyph loads through the 8-slot overflow ring buffer, which
  // thrashes for any text with more than 8 unique chars (i.e. all English),
  // floods the heap with short-lived bitmap allocations, and eventually
  // corrupts FreeRTOS state. The advance table persists across calls per
  // font, so the cost amortizes to ~ASCII-size after the first chunk.
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Form feed = hard page break. The quotes sidecar writes one before every quote,
    // so each saved quote opens its own page instead of running on from the last one.
    // Reached with lines already on the page: stop, leaving the marker as the next
    // page's first byte. Reached on an empty page: swallow it and fill this page.
    if (buffer[pos] == '\f') {
      if (!outLines.empty()) break;
      pos++;
      continue;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Emit at least one visual line for each source line (including blank lines),
    // then continue with wrapping when needed.
    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      int lineWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;  // Consumed entire display content
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextAdvanceX(cachedFontId, line.substr(0, breakPos).c_str(),
                                                      EpdFontFamily::REGULAR) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          // Make sure we don't break in the middle of a UTF-8 sequence
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      // Skip space at break point
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    // Determine how much of the source buffer we consumed
    if (line.empty()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
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
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::REGULAR);
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

  // The read timer starts when the page is actually on the panel, not at the press.
  if (statsTrackingActive) statsSession.pageShown(millis(), reading_stats::currentLocalDateTime());
  // Save progress
  saveProgress();
}

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

void TxtReaderActivity::runPowerDoubleClick() {
  // The pop-up owns the buttons while it is up; a double click must not act underneath it.
  if (settingsPopup.isActive()) return;

  switch (simple_reader_shortcut::resolve(SETTINGS.doubleClickPowerFunction, /*supportsStatusBarToggle=*/true)) {
    case simple_reader_shortcut::Action::ToggleStatusBar:
      SETTINGS.sbEnabled = SETTINGS.sbEnabled ? 0 : 1;
      SETTINGS.saveToFile();
      // The bar's band overlaps the reading margin, so showing or hiding it changes the
      // viewport and therefore how many lines fit a page. Re-index rather than repaint,
      // and keep the reader on the byte it was showing (the font-change path already
      // does exactly this).
      relayoutForFontChange();
      break;
    case simple_reader_shortcut::Action::WallpaperHold:
      SETTINGS.wallpaperRotationPaused = SETTINGS.wallpaperRotationPaused ? 0 : 1;
      SETTINGS.saveToFile();
      GUI.drawPopup(renderer, SETTINGS.wallpaperRotationPaused ? tr(STR_ROTATION_PAUSED) : tr(STR_ROTATION_RESUMED));
      requestUpdate();
      break;
    case simple_reader_shortcut::Action::None:
      // Reachable for Hold Wallpaper only: arming asks whether a wallpaper path exists,
      // and the card read that confirms the file is still there happens here.
      GUI.drawPopup(renderer, tr(STR_NOT_AVAILABLE));
      requestUpdate();
      break;
  }
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

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
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
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
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
  d.bookPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  d.bookTitle = txt->getTitle();
  // Paperback Look (status bar): thicken only status-bar glyphs, then reset.
  renderer.setPaperbackLook(SETTINGS.paperbackLookStatus);
  GUI.drawStatusBarV2(renderer, d);
  renderer.setPaperbackLook(false);
}

void TxtReaderActivity::saveProgress() const {
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
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

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (using serialization module):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID (to invalidate cache on font change)
  // - int32_t: screen margin (to invalidate cache on margin change)
  // - uint8_t: paragraph alignment (to invalidate cache on alignment change)
  // - uint32_t: total pages count
  // - N * uint32_t: page offsets

  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  // Read and validate header using serialization module
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  // Read page offsets
  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  // Write header using serialization module
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  // Write page offsets
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
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
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
