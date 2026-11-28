#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "ReaderToolbarSteps.h"
#include "util/ReadingPercent.h"

// The toolbar reader menu: a sheet over the page with an optional Return chip, a scrub row,
// the chapter and place, and four tiles (Contents, Bookmark, Text, More). See ReaderToolbarUi.

bool EpubReaderActivity::toolbarScrubsPages() const {
  return epub && section && reader_toolbar::scrubsPages(epub->getSpineItemsCount());
}

bool EpubReaderActivity::openToolbar() {
  // Needs a page on screen to paint over (not the End of Book screen); while one is still
  // being built the list menu is what a press gets.
  if (SETTINGS.readerMenuStyle != CrossPointSettings::READER_MENU_TOOLBAR || !section || !epub || toolbarOpen ||
      currentSpineIndex >= epub->getSpineItemsCount()) {
    return false;
  }
  // Heap while the sheet is up, freed on close; no stack or static home, since most reading
  // never opens it. Nothrow: a failed allocation falls back to the list menu.
  auto ui = makeUniqueNoThrow<ReaderToolbarUi>(renderer);
  if (!ui) {
    LOG_ERR("ERS", "OOM: toolbar");
    return false;
  }
  {
    RenderLock lock(*this);
    ui->begin();
    toolbarUi = std::move(ui);
    toolbarOpen = true;
    toolbarPaintOnly = true;
    toolbarJumped = false;
    toolbarScrubPermille = -1;
    toolbarScrubLastTarget = -1;
    toolbarScrubLastPercent = -1;
    // Buttons show a cursor from the start; touch boards only once a button moves it.
    toolbarFocus = mappedInput.hasTouch() ? -1 : reader_toolbar::Contents;
  }
  requestUpdate();
  return true;
}

void EpubReaderActivity::closeToolbar(const bool repaint) {
  {
    RenderLock lock(*this);
    toolbarOpen = false;
    toolbarPaintOnly = false;
    toolbarScrubPermille = -1;
    toolbarScrubLastTarget = -1;
    toolbarScrubLastPercent = -1;
    toolbarUi.reset();
  }
  if (repaint) {
    scheduleGhostCleanup();  // the sheet was drawn straight onto the page
    requestUpdate();
  }
}

void EpubReaderActivity::paintToolbar() {
  // Render task, the page already in the framebuffer.
  if (!toolbarOpen || !toolbarUi || !section || !epub) return;
  const bool byPage = toolbarScrubsPages();
  const int totalPages = section->estimatedTotalPages();
  std::string chapterTitle = tr(STR_UNNAMED);
  char info[32];
  int progressPermille = 0;

  if (toolbarScrubPermille >= 0) {
    progressPermille = toolbarScrubPermille;
    if (byPage) {
      const int previewPage = reader_toolbar::pageForPermille(toolbarScrubPermille, totalPages);
      const int tocIndex = section->getTocIndexForPage(previewPage);
      if (tocIndex != -1) chapterTitle = epub->getTocItem(tocIndex).title;
      const float spineProgress =
          totalPages > 1 ? static_cast<float>(previewPage) / static_cast<float>(totalPages - 1) : 0.0f;
      const int previewPercent = reading_percent::toPercent(epub->calculateProgress(currentSpineIndex, spineProgress));
      snprintf(info, sizeof(info), "%d/%d   %d%%", previewPage + 1, totalPages, previewPercent);
    } else {
      const int previewSpine = reader_toolbar::spineForPermille(toolbarScrubPermille, epub->getSpineItemsCount());
      const int tocIndex = epub->getTocIndexForSpineIndex(previewSpine);
      if (tocIndex != -1) chapterTitle = epub->getTocItem(tocIndex).title;
      const int previewPercent = reading_percent::toPercent(epub->calculateProgress(previewSpine, 0.0f));
      snprintf(info, sizeof(info), "%d%%", previewPercent);
    }
  } else {
    const int tocIndex = section->getTocIndexForPage(section->currentPage);
    if (tocIndex != -1) chapterTitle = epub->getTocItem(tocIndex).title;
    const int percent = bookPercent();
    snprintf(info, sizeof(info), "%d/%d   %d%%", section->currentPage + 1, totalPages, percent);
    progressPermille = byPage ? reader_toolbar::permilleForPage(section->currentPage, totalPages) : percent * 10;
  }

  ReaderToolbarUi::Model model;
  model.chapterTitle = chapterTitle.c_str();
  model.pageInfo = info;
  model.progressPermille = progressPermille;
  model.activeTool = toolbarFocus;
  model.bookmarked = isCurrentPageBookmarked();
  model.canReturn = !returnHistory.empty();
  model.returnLabel = tr(STR_RETURN);
  toolbarUi->setModel(model);
  toolbarUi->render();
  // FAST: only the sheet changes, and a full cleanup would flash the page above it.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubReaderActivity::toolbarGotoSpine(const int spine) {
  const int target = reader_toolbar::clampSpine(spine, epub->getSpineItemsCount());
  if (target == currentSpineIndex) return;
  {
    RenderLock lock(*this);
    if (!toolbarJumped) {
      recordJumpOrigin(true);
      toolbarJumped = true;
    }
    clearDeferredReposition();
    pendingAnchor.clear();
    nextPageNumber = 0;
    currentSpineIndex = target;
    section.reset();
  }
  requestUpdate();  // the new chapter's page, then the sheet over it
}

void EpubReaderActivity::toolbarGotoPage(const int page) {
  if (!section) return;
  const int total = section->estimatedTotalPages();
  const int target = reader_toolbar::clampPage(page, total);
  if (target == section->currentPage) return;
  {
    RenderLock lock(*this);
    if (!toolbarJumped) {
      recordJumpOrigin(true);
      toolbarJumped = true;
    }
    clearDeferredReposition();
    pendingAnchor.clear();
    section->currentPage = target;
  }
  requestUpdate();
}

void EpubReaderActivity::toolbarReturn() {
  if (returnHistory.empty()) return;
  {
    RenderLock lock(*this);
    std::optional<ReaderReturnHistory::Position> here;
    if (currentPageSpineIndex >= 0 && currentPageVisibleOffset) {
      here = ReaderReturnHistory::Position{currentPageSpineIndex, *currentPageVisibleOffset};
    }
    const auto target = returnHistory.beginReturn(here);
    if (!target) return;
    jumpToContentOffset(target->spineIndex, target->contentOffset);
  }
  toolbarScrubPermille = -1;
  toolbarScrubLastTarget = -1;
  toolbarScrubLastPercent = -1;
  requestUpdate();
}

void EpubReaderActivity::reopenToolbar(void* self, const int tool) {
  auto* reader = static_cast<EpubReaderActivity*>(self);
  // The sheet paints over the page, so it waits for the page to be drawn again.
  if (!reader->pageOnScreen.load(std::memory_order_acquire) || !reader->section) {
    reader->reopenOnPage = MenuReturn{&EpubReaderActivity::reopenToolbar, self, tool};
    return;
  }
  // Buttons land on the tile that was left; touch boards show no cursor, as on any open.
  if (reader->openToolbar() && !reader->mappedInput.hasTouch()) reader->toolbarFocus = tool;
}

void EpubReaderActivity::runToolbarTool(const int tool) {
  if (tool == reader_toolbar::Bookmark) {
    if (sortesMode) {
      closeToolbar(false);
      blockSortesAction();
      return;
    }
    addBookmark();
    return;
  }
  // Each other tile leaves for a screen of its own; that screen repaints the page when it
  // closes, and Back from it brings the sheet back on this tile.
  closeToolbar(false);
  MenuReturn::Scope backToToolbar(&EpubReaderActivity::reopenToolbar, this, tool);
  switch (tool) {
    case reader_toolbar::Contents:
      openChapterSelection();
      break;
    case reader_toolbar::Text:
      openReaderSettings();
      break;
    default:
      openReaderMenuList();
      break;
  }
}

bool EpubReaderActivity::handleToolbarInput() {
  if (reopenOnPage && section && pageOnScreen.load(std::memory_order_acquire)) {
    std::exchange(reopenOnPage, MenuReturn{}).run();
    return true;
  }
  if (!toolbarOpen || !toolbarUi) return false;
  using Button = MappedInputManager::Button;
  const bool byPage = toolbarScrubsPages();
  const auto routed = toolbarUi->route(mappedInput);
  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      closeToolbar(true);
      return true;
    case ReaderToolbarUi::Event::Tool:
      runToolbarTool(routed.value);
      return true;
    case ReaderToolbarUi::Event::Return:
      toolbarReturn();
      return true;
    case ReaderToolbarUi::Event::PrevChapter:
      if (byPage) {
        toolbarGotoPage(section->currentPage - 1);
      } else {
        toolbarGotoSpine(currentSpineIndex - 1);
      }
      return true;
    case ReaderToolbarUi::Event::NextChapter:
      if (byPage) {
        toolbarGotoPage(section->currentPage + 1);
      } else {
        toolbarGotoSpine(currentSpineIndex + 1);
      }
      return true;
    case ReaderToolbarUi::Event::ScrubPreview: {
      int target = 0;
      int percent = 0;
      if (byPage) {
        const int totalPages = section->estimatedTotalPages();
        target = reader_toolbar::pageForPermille(routed.permille, totalPages);
        const float sp = totalPages > 1 ? static_cast<float>(target) / static_cast<float>(totalPages - 1) : 0.0f;
        percent = reading_percent::toPercent(epub->calculateProgress(currentSpineIndex, sp));
      } else {
        target = reader_toolbar::spineForPermille(routed.permille, epub->getSpineItemsCount());
        percent = reading_percent::toPercent(epub->calculateProgress(target, 0.0f));
      }
      toolbarScrubPermille = routed.permille;
      if (target != toolbarScrubLastTarget || percent != toolbarScrubLastPercent) {
        toolbarScrubLastTarget = target;
        toolbarScrubLastPercent = percent;
        toolbarPaintOnly = true;
        requestUpdate();
      }
      return true;
    }
    case ReaderToolbarUi::Event::Scrub:
      toolbarScrubPermille = -1;
      toolbarScrubLastTarget = -1;
      toolbarScrubLastPercent = -1;
      if (byPage) {
        toolbarGotoPage(reader_toolbar::pageForPermille(routed.permille, section->estimatedTotalPages()));
      } else {
        toolbarGotoSpine(reader_toolbar::spineForPermille(routed.permille, epub->getSpineItemsCount()));
      }
      return true;
    case ReaderToolbarUi::Event::None:
      break;
  }
  if (toolbarScrubPermille >= 0) {
    int hx = 0;
    int hy = 0;
    if (!mappedInput.isScreenTouchHeld(hx, hy)) {
      toolbarScrubPermille = -1;
      toolbarScrubLastTarget = -1;
      toolbarScrubLastPercent = -1;
      toolbarPaintOnly = true;
      requestUpdate();
    }
  }
  if (routed.routed) return true;

  // Buttons act on the press, like the Quick Menu: whatever opened the sheet has already
  // had its release.
  if (mappedInput.wasPressed(Button::Back)) {
    closeToolbar(true);
  } else if (mappedInput.wasPressed(Button::Confirm)) {
    runToolbarTool(std::max(toolbarFocus, 0));
  } else if (mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Right)) {
    const int direction = mappedInput.wasPressed(Button::Right) ? 1 : -1;
    toolbarFocus = toolbarFocus < 0 ? reader_toolbar::Contents : reader_toolbar::stepTool(toolbarFocus, direction);
    toolbarPaintOnly = true;
    requestUpdate();
  } else if (mappedInput.wasPressed(Button::Up)) {
    if (byPage) {
      toolbarGotoPage(section->currentPage - 1);
    } else {
      toolbarGotoSpine(currentSpineIndex - 1);
    }
  } else if (mappedInput.wasPressed(Button::Down)) {
    if (byPage) {
      toolbarGotoPage(section->currentPage + 1);
    } else {
      toolbarGotoSpine(currentSpineIndex + 1);
    }
  }
  return true;
}
