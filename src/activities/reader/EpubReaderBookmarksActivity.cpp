#include "EpubReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "../../util/BookmarkFile.h"
#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
}  // namespace

void EpubReaderBookmarksActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) return;

  if (!BookmarkFile::load(epubPath, bookmarks)) {
    bookmarks.shrink_to_fit();
  }
  LOG_DBG("EPB", "Loaded %d bookmarks for book: %s", static_cast<int>(bookmarks.size()), epubPath.c_str());

  requestUpdate();
}

void EpubReaderBookmarksActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  subtitles.clear();
}

int EpubReaderBookmarksActivity::listCount() const { return static_cast<int>(bookmarks.size()); }

void EpubReaderBookmarksActivity::refreshRows(const bool portrait) {
  const int count = listCount();
  subtitles.assign(count, std::string());
  rows.assign(count, fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    const BookmarkEntry& bookmark = bookmarks[i];
    const int tocIndex = epub ? epub->getTocIndexForSpineIndex(bookmark.computedSpineIndex) : -1;
    const std::string tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);
    std::string subtitle =
        std::to_string(static_cast<int>(std::clamp(bookmark.percentage, 0.0f, 1.0f) * 100.0f + 0.5f)) + "% - ";
    if (bookmark.computedChapterPageCount > 0) {
      subtitle += std::to_string(bookmark.computedChapterProgress + 1) + "/" +
                  std::to_string(bookmark.computedChapterPageCount) + " - ";
    }
    subtitles[i] = subtitle + tocTitle;

    rows[i].label = bookmark.summary.c_str();
    rows[i].subtitle = subtitles[i].c_str();
    // Icons only in portrait: the icon assets cannot be rotated, so in the
    // landscape orientations they would face the wrong way.
    if (portrait) rows[i].icon = listIconFor(UIIcon::Bookmark, 32);
    rows[i].actionValue = static_cast<int16_t>(i);
  }
}

void EpubReaderBookmarksActivity::buildScreen(UiScreen& screen) {
  if (bookmarks.empty()) {
    screen.centeredText(tr(STR_NO_BOOKMARKS));
    return;
  }

  refreshRows(renderer.getOrientation() == GfxRenderer::Orientation::Portrait);

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void EpubReaderBookmarksActivity::openBookmark(const int index) {
  if (index < 0 || index >= listCount()) return;
  const BookmarkEntry& bookmark = bookmarks[index];
  ProgressChangeResult result{};
  result.xpath = bookmark.xpath;
  result.percentage = bookmark.percentage;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = bookmark.hasVisibleTextOffset;
  result.visibleTextOffset = bookmark.visibleTextOffset;
  // The offset is spine-relative, so carry its spine even when the legacy page
  // hints below are stale. The reader validates the index.
  result.spineIndex = bookmark.computedSpineIndex;
  if (bookmark.computedChapterPageCount > 0 && bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
      epub && bookmark.computedSpineIndex < epub->getSpineItemsCount()) {
    result.page = bookmark.computedChapterProgress;
    result.totalPages = bookmark.computedChapterPageCount;
  }
  app.clearTapFlash();
  setResult(std::move(result));
  finish();
}

void EpubReaderBookmarksActivity::activateIndex(const int index) { openBookmark(index); }

void EpubReaderBookmarksActivity::onRowLongPress(const int index) { promptDelete(index); }

void EpubReaderBookmarksActivity::promptDelete(const int index) {
  if (index < 0 || index >= listCount()) return;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_BOOKMARK), options, 2, 0, [this, index](const int choice) {
    if (choice == 1) deleteBookmark(index);
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderBookmarksActivity::deleteBookmark(const int index) {
  if (index < 0 || index >= listCount()) return;
  {
    // The published rows borrow strings from `bookmarks` and `subtitles`;
    // erasing under the lock keeps the render task from reading a freed one.
    RenderLock lock(*this);
    bookmarks.erase(bookmarks.begin() + index);
    rows.clear();
    subtitles.clear();
    closeRouting();
    if (nav.selected >= listCount() && nav.selected > 0) nav.selected = listCount() - 1;
    nav.follow(listCount());
  }
  if (!BookmarkFile::save(epubPath, bookmarks)) {
    LOG_ERR("EPB", "Failed to save bookmarks after delete");
  }
  if (bookmarks.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}

bool EpubReaderBookmarksActivity::handleCustomInput() {
  return confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderBookmarksActivity::handleButtons() {
  // Back carries no hold here, so it closes on the press. Confirm cannot:
  // holding it deletes, so which action was meant is only known on release.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openBookmark(nav.selected);
    return true;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    promptDelete(nav.selected);
    return true;
  }
  return false;
}

void EpubReaderBookmarksActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

ListChrome EpubReaderBookmarksActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_BOOKMARKS);
  if (bookmarks.empty()) {
    // Nothing to open and nothing to delete, so neither is offered.
    chrome.confirmHint = "";
    return chrome;
  }
  chrome.footnotes[0] = tr(STR_HOLD_OPEN_TO_DELETE);
  return chrome;
}

bool EpubReaderBookmarksActivity::drawOverlay() { return confirmPopup.processRender(renderer, mappedInput); }
