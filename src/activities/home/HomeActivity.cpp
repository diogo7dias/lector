#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Recent books loaded for the LIST home. The renderer scrolls through them one
// row at a time; more than a couple dozen is never realistically scrolled to.
constexpr int kHomeListMaxBooks = 20;

// "Lector [semver]" for the list-home header, mirroring the DX34 Lector home.
// CROSSPOINT_VERSION is "…-<branch>-<semver>"; take the trailing semver.
std::string getHomeHeaderVersionLabel() {
  const std::string rawVersion = CROSSPOINT_VERSION;
  const size_t dashPos = rawVersion.find_last_of('-');
  const std::string semver =
      (dashPos != std::string::npos && dashPos + 1 < rawVersion.size()) ? rawVersion.substr(dashPos + 1) : rawVersion;
  return "Lector [" + semver + "]";
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 3;  // File Browser, File transfer, Settings (Recent Books moved into the file browser)
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  pendingFullRefresh = true;  // clear ghosting on the first paint back at Home
  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int loadCount =
      (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST) ? kHomeListMaxBooks : metrics.homeRecentBooksCount;
  loadRecentBooks(loadCount);

  // The list home reserves selector 0 for the pages button. Default entry lands on
  // the first book (or the first menu item when there are no books) — never the
  // pages tally, which the user reaches by pressing up from the top book.
  const int pagesSlot = (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST) ? 1 : 0;
  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = (initialMenuItem == HomeMenuItem::NONE)
                      ? pagesSlot
                      : pagesSlot + base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool listLayout = SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST;
  // The list home reserves selector 0 for the "pages read" button; the cover home
  // has no such button, so books/menu start at selector 0 there.
  const int pagesSlot = listLayout ? 1 : 0;
  const int menuCount = getMenuItemCount() + pagesSlot;

  buttonNavigator.onNext([this, menuCount, listLayout, pagesSlot] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    if (listLayout) {
      // Books occupy selector slots [pagesSlot, pagesSlot + bookCount). Keep the
      // selected row inside the renderer's visible band by nudging scrollOffset.
      // The renderer auto-adjusts downward, but a wrap back to the top must be
      // handled here.
      const int bookCount = static_cast<int>(recentBooks.size());
      const int bookSel = selectorIndex - pagesSlot;
      if (bookSel >= 0 && bookSel < bookCount) {
        if (bookSel > lastVisibleBookIdx) scrollOffset++;
        if (bookSel < firstVisibleBookIdx) scrollOffset = bookSel;
        scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
      }
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount, listLayout, pagesSlot] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    if (listLayout) {
      const int bookCount = static_cast<int>(recentBooks.size());
      const int bookSel = selectorIndex - pagesSlot;
      if (bookSel >= 0 && bookSel < bookCount) {
        if (bookSel < firstVisibleBookIdx) scrollOffset = bookSel;
        scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
      }
    }
    requestUpdate();
  });

  // Back on a selected recent book removes it from the list (list home only).
  if (listLayout && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    const int bookSel = selectorIndex - pagesSlot;
    if (bookSel >= 0 && bookSel < static_cast<int>(recentBooks.size())) {
      const std::string path = recentBooks[bookSel].path;
      if (!path.empty()) {
        promptRemoveBook(path, recentBooks[bookSel].title);
        return;
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Pages button (list home, selector 0): zero the tally and persist immediately
    // so a reset survives a power-off before the next state save.
    if (listLayout && selectorIndex == 0) {
      APP_STATE.sessionPagesRead = 0;
      APP_STATE.saveToFile();
      requestUpdate();
      return;
    }
    const int bookSel = selectorIndex - pagesSlot;
    if (bookSel >= 0 && bookSel < static_cast<int>(recentBooks.size())) {
      onSelectBook(recentBooks[bookSel].path);
    } else {
      const int menuIndex = bookSel - static_cast<int>(recentBooks.size());
      switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
        case HomeMenuItem::FILE_BROWSER:
          onFileBrowserOpen();
          break;
        case HomeMenuItem::OPDS_BROWSER:
          onOpdsBrowserOpen();
          break;
        case HomeMenuItem::FILE_TRANSFER:
          onFileTransferOpen();
          break;
        case HomeMenuItem::SETTINGS_MENU:
          onSettingsOpen();
          break;
        default:
          break;
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // ---- LIST home (Lector): scrolling recent-books list + bottom menu ----
  if (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
    const std::string versionLabel = getHomeHeaderVersionLabel();
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + 5, versionLabel.c_str());

    // Running "pages read" tally as a focusable button (selector 0), inline just
    // after the version label. Confirm on it resets the tally to 0. DX34 styling: a
    // thin bordered "Pages:" label tile plus an inverted (black background, white
    // text) count tile — the same look as the [NN%] badge in the recent-book list.
    // When selected the label tile fills solid black too so the whole pill reads as
    // one highlighted block.
    const int versionLabelWidth = renderer.getTextWidth(UI_10_FONT_ID, versionLabel.c_str());
    const bool pagesSelected = selectorIndex == 0;
    std::string pagesLabel = tr(STR_HOME_PAGES_PREFIX);  // "Pages: " — drop the trailing space for the tile.
    while (!pagesLabel.empty() && pagesLabel.back() == ' ') pagesLabel.pop_back();
    const std::string countText = std::to_string(APP_STATE.sessionPagesRead);
    const int tileTextY = metrics.topPadding + 5;
    const int tilePad = 6;
    const int tileGap = 5;
    const int tileH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
    const int tileY = tileTextY - 3;

    // drawRect(lineWidth) paints its outer bottom border at tileY + tileH (an
    // inclusive tileH+1-tall box), whereas fillRect covers only tileH rows. Fill
    // one row taller so the filled tiles' bottoms align with the bordered tile.
    const int fillH = tileH + 1;
    const int labelTileX = metrics.contentSidePadding + versionLabelWidth + 14;
    const int labelTextW = renderer.getTextWidth(UI_10_FONT_ID, pagesLabel.c_str());
    const int labelTileW = labelTextW + tilePad * 2;
    if (pagesSelected) {
      renderer.fillRect(labelTileX, tileY, labelTileW, fillH, true);
    } else {
      renderer.drawRect(labelTileX, tileY, labelTileW, tileH, 2, true);
    }
    renderer.drawText(UI_10_FONT_ID, labelTileX + tilePad, tileTextY, pagesLabel.c_str(), !pagesSelected);

    // Count tile: always an inverted chip (black fill, white text), centered.
    const int countTileX = labelTileX + labelTileW + tileGap;
    const int countTextW = renderer.getTextWidth(UI_10_FONT_ID, countText.c_str());
    const int countTileW = countTextW + tilePad * 2;
    renderer.fillRect(countTileX, tileY, countTileW, fillH, true);
    renderer.drawText(UI_10_FONT_ID, countTileX + (countTileW - countTextW) / 2, tileTextY, countText.c_str(), false);

    // Clock sits just left of the battery cluster (icon + "%"), sharing the
    // battery's row and font size (UI_10). X3-only: DS3231 RTC, inert on X4.
    // drawHeader draws the battery at the right edge; mirror its geometry here
    // and reserve "100%" width so the clock never overlaps the percentage.
    if (halClock.isAvailable()) {
      char timeBuf[9];
      if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
        const int batteryIconLeft = pageWidth - 12 - metrics.batteryWidth;
        const int batteryTextWidth = renderer.getTextWidth(UI_10_FONT_ID, "100%");
        const int batteryClusterLeft = batteryIconLeft - batteryTextWidth - 4;
        const int clockWidth = renderer.getTextWidth(UI_10_FONT_ID, timeBuf);
        renderer.drawText(UI_10_FONT_ID, batteryClusterLeft - 12 - clockWidth, metrics.topPadding + 5, timeBuf);
      }
    }

    // Menu items (no per-book "Continue Reading" — the list itself is that).
    // Recent Books is reached from the top of the file browser, not from here.
    std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
    std::vector<UIIcon> menuIcons = {Folder, Transfer, Settings};
    if (hasOpdsServers) {
      menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
      menuIcons.insert(menuIcons.begin() + 1, Library);
    }

    // Menu block is pinned to the bottom; the list fills the band above it.
    const int menuCount = static_cast<int>(menuItems.size());
    const int menuBlockHeight = metrics.verticalSpacing + menuCount * metrics.menuRowHeight +
                                (menuCount > 0 ? (menuCount - 1) * metrics.menuSpacing : 0);
    constexpr int menuBottomGap = 8;
    const int menuY = pageHeight - metrics.buttonHintsHeight - menuBottomGap - menuBlockHeight;

    const int recentAreaY = metrics.topPadding + metrics.homeTopPadding;
    constexpr int recentAreaBottomGap = 8;
    const int recentAreaHeight = std::max(0, menuY - recentAreaBottomGap - recentAreaY);

    // Selector 0 is the pages button, so book rows live at selector [1, 1+count).
    // Pass a book-based index (selectorIndex - 1); on the pages button this is -1,
    // which highlights no row.
    const auto vis = GUI.drawRecentBookList(renderer, Rect{0, recentAreaY, pageWidth, recentAreaHeight}, recentBooks,
                                            selectorIndex - 1, scrollOffset);
    firstVisibleBookIdx = vis.firstVisible;
    lastVisibleBookIdx = vis.lastVisible;
    scrollOffset = vis.firstVisible;

    const int menuSelected = selectorIndex - 1 - static_cast<int>(recentBooks.size());
    GUI.drawButtonMenu(
        renderer, Rect{0, menuY, pageWidth, menuBlockHeight}, menuCount, menuSelected,
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });

    const int bookSel = selectorIndex - 1;  // selector 0 is the pages button
    const bool bookSelected =
        bookSel >= 0 && bookSel < static_cast<int>(recentBooks.size()) && !recentBooks[bookSel].path.empty();
    const char* backLabel = bookSelected ? tr(STR_REMOVE_BUTTON) : "";
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(pendingFullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
    pendingFullRefresh = false;
    return;
  }

  // ---- SINGLE_COVER home (upstream CrossPoint): one big current-book cover ----
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically (Recent Books now lives in the file browser).
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 1, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(pendingFullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  pendingFullRefresh = false;

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) {
  // Book parse/layout takes a moment; show the "Opening..." banner (the same
  // full-width popup style as every other popup) so the tap has immediate
  // feedback. The panel holds this frame until the reader's first paint lands.
  GUI.drawPopup(renderer, tr(STR_OPENING));
  activityManager.goToReader(path);
}

void HomeActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) {
      return;
    }
    if (RECENT_BOOKS.removeByPath(path)) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      const int loadCount = (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST)
                                ? kHomeListMaxBooks
                                : metrics.homeRecentBooksCount;
      loadRecentBooks(loadCount);
      // Removal only happens on the list home, which reserves selector 0 for the
      // pages button, so include that slot when clamping.
      const int pagesSlot = (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST) ? 1 : 0;
      const int menuCount = getMenuItemCount() + pagesSlot;
      if (selectorIndex >= menuCount) {
        selectorIndex = std::max(0, menuCount - 1);
      }
      const int maxOffset = std::max(0, static_cast<int>(recentBooks.size()) - 1);
      if (scrollOffset > maxOffset) {
        scrollOffset = maxOffset;
      }
      pendingFullRefresh = true;  // clear the removed row's ghost
      requestUpdate();
    }
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
