#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "ReaderToolbarSteps.h"

// The toolbar reader menu: a sheet over the page with a scrub row, the chapter and place,
// and three tiles that open lector's own screens (Contents = the chapter list, Text = this
// book's text settings, More = the tabbed list menu). See ReaderToolbarUi.

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
  renderer.waitRefreshComplete();
  std::string chapterTitle = tr(STR_UNNAMED);
  const int tocIndex = section->getTocIndexForPage(section->currentPage);
  if (tocIndex != -1) chapterTitle = epub->getTocItem(tocIndex).title;
  const int percent = bookPercent();
  char info[32];
  snprintf(info, sizeof(info), "%d/%d   %d%%", section->currentPage + 1, section->estimatedTotalPages(), percent);

  ReaderToolbarUi::Model model;
  model.chapterTitle = chapterTitle.c_str();
  model.pageInfo = info;
  model.progressPermille = percent * 10;
  model.activeTool = toolbarFocus;
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
  // Each tile leaves for a screen of its own; that screen repaints the page when it closes,
  // and Back from it brings the sheet back on this tile.
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
  const auto routed = toolbarUi->route(mappedInput);
  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      closeToolbar(true);
      return true;
    case ReaderToolbarUi::Event::Tool:
      runToolbarTool(routed.value);
      return true;
    case ReaderToolbarUi::Event::PrevChapter:
      toolbarGotoSpine(currentSpineIndex - 1);
      return true;
    case ReaderToolbarUi::Event::NextChapter:
      toolbarGotoSpine(currentSpineIndex + 1);
      return true;
    case ReaderToolbarUi::Event::Scrub:
      toolbarGotoSpine(reader_toolbar::spineForPermille(routed.permille, epub->getSpineItemsCount()));
      return true;
    case ReaderToolbarUi::Event::None:
      break;
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
    toolbarGotoSpine(currentSpineIndex - 1);
  } else if (mappedInput.wasPressed(Button::Down)) {
    toolbarGotoSpine(currentSpineIndex + 1);
  }
  return true;
}
