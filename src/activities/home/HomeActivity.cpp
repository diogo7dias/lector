#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
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
#include "activities/reader/ReaderUtils.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TopEdgeInset.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/Wallpaper.h"

namespace {
// Recent books loaded for the LIST home. The renderer scrolls through them one
// row at a time; more than a couple dozen is never realistically scrolled to.
constexpr int kHomeListMaxBooks = 20;

// "Lector [version]" for the list-home header, mirroring the DX34 Lector home.
// CROSSPOINT_VERSION is now a plain semver (e.g. "0.5.0") for every build env,
// but keep the trailing-token fallback so any legacy "…-<suffix>" string still
// renders the meaningful tail rather than the whole raw string.
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

void HomeActivity::pumpRecentCovers(int coverHeight) {
  // Chunked cover-thumb pump, driven from loop(): generates AT MOST ONE
  // missing thumbnail per call, then returns so input is serviced between
  // grinds. The old whole-list loop ran inside render() and could hold the
  // loop for many seconds right after the wake paint — a "menu visible but
  // buttons dead" window, which the hold-boot rule forbids.
  while (coverGenIndex < recentBooks.size()) {
    RecentBook& book = recentBooks[coverGenIndex];
    ++coverGenIndex;
    if (book.coverBmpPath.empty()) continue;
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (Storage.exists(coverPath.c_str())) continue;

    // One real grind (epub/xtc open + image decode + scale, seconds). Latch
    // any taps so they act the moment this book finishes.
    mappedInput.pumpWaitInput();
    const int progressPct = 10 + static_cast<int>(coverGenIndex - 1) * (90 / static_cast<int>(recentBooks.size()));
    bool attempted = false;
    bool success = false;
    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      // Skip loading css since we only need metadata here
      epub.load(false, true);
      GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), PopupRefresh::Temporary);
      GUI.fillBottomProgress(renderer, progressPct);
      success = epub.generateThumbBmp(coverHeight);
      attempted = true;
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), PopupRefresh::Temporary);
        GUI.fillBottomProgress(renderer, progressPct);
        success = xtc.generateThumbBmp(coverHeight);
        attempted = true;
      }
    }
    if (!attempted) continue;
    if (!success) {
      RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
      book.coverBmpPath = "";
    }
    coverRendered = false;
    requestUpdate();
    return;  // one grind per loop pass; input runs before the next one
  }
  recentsLoaded = true;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  pendingFullRefresh = true;  // clear ghosting on the first paint back at Home
  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int loadCount =
      (SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST) ? kHomeListMaxBooks : metrics.homeRecentBooksCount;
  loadRecentBooks(loadCount);

  // Both layouts reserve selector 0 for the pages tally. LIST slots: 1..bookCount =
  // books, then menu. SINGLE_COVER slots: 1 = centre cover, 2.. = menu. Default entry
  // lands on the first book / the cover (or the first menu item when there are none).
  // Sleep-folder overflow notices: one-shot "moved to pause" toast + the sticky
  // over-limit state that drives the LIST warning card (and its extra selector slot).
  maybeShowWallpaperPauseToast();
  refreshSleepOverLimit();

  const bool listLayout = SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST;
  const int bookCount = static_cast<int>(recentBooks.size());
  coverflowIndex = 0;
  if (initialMenuItem != HomeMenuItem::NONE) {
    const int menuIdx = menuItemToIndex(initialMenuItem, hasOpdsServers);
    selectorIndex = listLayout ? (firstBookList() + bookCount + menuIdx) : (2 + menuIdx);
  } else if (listLayout) {
    selectorIndex = firstBookList();  // first book (or first menu item when there are no books)
  } else {
    selectorIndex = (bookCount > 0) ? 1 : 2;  // cover slot, else first menu item
  }

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
  // Wake press meter: on the first press after the entry paint, stamp a terse
  // band with the numbers that matter for the "menu visible but dead" hunt -
  // when the menu was physically ready, how long after that the press edge
  // came, and edge-to-handled latency (should be ~0 when nothing blocks the
  // loop; a big value = something ground on after the paint). Photo-able,
  // like the reader wake overlay.
  if (!wakePressMeterDone_ && SETTINGS.wakeDiagnostics && wakeMenuReadyAt_ != 0 && mappedInput.wasAnyPressed()) {
    wakePressMeterDone_ = true;
    const unsigned long now = millis();
    const unsigned long edge = mappedInput.lastPressStart();
    const unsigned long sinceMenu = edge >= wakeMenuReadyAt_ ? edge - wakeMenuReadyAt_ : 0;
    const unsigned long latency = now >= edge ? now - edge : 0;
    LOG_INF("DIAG", "HOME wake: menu@%lums press@+%lums lat=%lums", wakeMenuReadyAt_, sinceMenu, latency);
    char line[96];
    snprintf(line, sizeof(line), "menu %lu press +%lu lat %lu", wakeMenuReadyAt_, sinceMenu, latency);
    const int h = renderer.getLineHeight(SMALL_FONT_ID) + 4;
    const int y = renderer.getScreenHeight() - h;
    renderer.fillRect(0, y, renderer.getScreenWidth(), h, false);
    renderer.drawText(SMALL_FONT_ID, 4, y + 2, line, true);
    renderer.present(RefreshIntent::TransientBand, 0, y, renderer.getScreenWidth(), h);
  }

  const bool listLayout = SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST;
  const int bookCount = static_cast<int>(recentBooks.size());

  // Coverflow home: top up missing cover thumbs one book per pass, so a press
  // is handled between grinds (menu visible = buttons live, LOCKED rule).
  if (!listLayout && firstRenderDone && !recentsLoaded) {
    pumpRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
  }

  // Both layouts reserve selector 0 for the pages tally.
  //  LIST:         [pages][book 0..N-1][menu 0..]  -> 1 + bookCount + menuItemsOnly
  //  SINGLE_COVER: [pages][cover][menu 0..]        -> 2 + menuItemsOnly
  const int menuItemsOnly = getMenuItemCount() - bookCount;  // Browse/Transfer/Settings (+OPDS)
  // LIST reserves slot 0 for pages + an optional warning card before the books.
  const int navSlots = listLayout ? (firstBookList() + bookCount + menuItemsOnly) : (2 + menuItemsOnly);

  // SINGLE_COVER: the side (X3) / page-turn (X4) buttons flip the centered book.
  // Kept ahead of the front-button nav so it never fights the menu selector.
  if (!listLayout && bookCount > 1) {
    const auto pt = ReaderUtils::detectPageTurn(mappedInput);
    if (pt.next) {
      coverflowIndex = std::min(coverflowIndex + 1, bookCount - 1);
      requestUpdate();
      return;
    }
    if (pt.prev) {
      coverflowIndex = std::max(coverflowIndex - 1, 0);
      requestUpdate();
      return;
    }
  }

  buttonNavigator.onNext([this, navSlots, listLayout, bookCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, navSlots);
    if (listLayout) {
      const int bookSel = selectorIndex - firstBookList();
      if (bookSel >= 0 && bookSel < bookCount) {
        if (bookSel > lastVisibleBookIdx) scrollOffset++;
        if (bookSel < firstVisibleBookIdx) scrollOffset = bookSel;
        scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
      }
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, navSlots, listLayout, bookCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, navSlots);
    if (listLayout) {
      const int bookSel = selectorIndex - firstBookList();
      if (bookSel >= 0 && bookSel < bookCount) {
        if (bookSel < firstVisibleBookIdx) scrollOffset = bookSel;
        scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, bookCount - 1)));
      }
    }
    requestUpdate();
  });

  // Back removes the focused book from recents.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (listLayout) {
      const int bookSel = selectorIndex - firstBookList();
      if (bookSel >= 0 && bookSel < bookCount && !recentBooks[bookSel].path.empty()) {
        promptRemoveBook(recentBooks[bookSel].path, recentBooks[bookSel].title);
        return;
      }
    } else if (selectorIndex == 1 && bookCount > 0 && !recentBooks[coverflowIndex].path.empty()) {
      promptRemoveBook(recentBooks[coverflowIndex].path, recentBooks[coverflowIndex].title);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Pages tile (selector 0, both layouts): zero the tally and persist now so a
    // reset survives a power-off before the next state save.
    if (selectorIndex == 0) {
      APP_STATE.sessionPagesRead = 0;
      APP_STATE.saveToFile();
      requestUpdate();
      return;
    }
    // LIST over-limit warning card (slot 1): open the random bulk-move keypad.
    if (listLayout && sleepOverLimit && selectorIndex == 1) {
      openSleepMoveKeypad();
      return;
    }
    int menuIndex;
    if (listLayout) {
      const int bookSel = selectorIndex - firstBookList();
      if (bookSel >= 0 && bookSel < bookCount) {
        onSelectBook(recentBooks[bookSel].path);
        return;
      }
      menuIndex = bookSel - bookCount;
    } else {
      if (selectorIndex == 1) {
        if (bookCount > 0) onSelectBook(recentBooks[coverflowIndex].path);
        return;
      }
      menuIndex = selectorIndex - 2;
    }
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

void HomeActivity::drawHomeTopLine(int pageWidth, bool pagesSelected) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // drawHeader now applies the X4 top-edge inset itself, so pass the raw
  // topPadding to it. headerY (with inset) still positions Home's own chrome
  // below — version label + pages tile — so they line up with the header.
  const int headerY = metrics.topPadding + topEdgeInset(gpio.deviceIsX4());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  const std::string versionLabel = getHomeHeaderVersionLabel();
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, headerY + 5, versionLabel.c_str());

  // Running "pages read" tally: a thin bordered "Pages" label tile + an inverted
  // count chip. Selectable (selector 0) — Confirm resets it to 0.
  const int versionLabelWidth = renderer.getTextWidth(UI_10_FONT_ID, versionLabel.c_str());
  std::string pagesLabel = tr(STR_HOME_PAGES_PREFIX);  // "Pages: " — drop trailing space for the tile.
  while (!pagesLabel.empty() && pagesLabel.back() == ' ') pagesLabel.pop_back();
  const std::string countText = std::to_string(APP_STATE.sessionPagesRead);
  const int tileTextY = headerY + 5;
  const int tilePad = 6;
  const int tileGap = 5;
  const int tileH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int tileY = tileTextY - 3;
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

  const int countTileX = labelTileX + labelTileW + tileGap;
  const int countTextW = renderer.getTextWidth(UI_10_FONT_ID, countText.c_str());
  const int countTileW = countTextW + tilePad * 2;
  renderer.fillRect(countTileX, tileY, countTileW, fillH, true);
  renderer.drawText(UI_10_FONT_ID, countTileX + (countTileW - countTextW) / 2, tileTextY, countText.c_str(), false);

  // Clock just left of the battery cluster (X3 DS3231; inert on X4).
  if (halClock.isAvailable()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      const int batteryIconLeft = pageWidth - 12 - metrics.batteryWidth;
      const int batteryTextWidth = renderer.getTextWidth(UI_10_FONT_ID, "100%");
      const int batteryClusterLeft = batteryIconLeft - batteryTextWidth - 4;
      const int clockWidth = renderer.getTextWidth(UI_10_FONT_ID, timeBuf);
      renderer.drawText(UI_10_FONT_ID, batteryClusterLeft - 12 - clockWidth, headerY + 5, timeBuf);
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
    drawHomeTopLine(pageWidth, /*pagesSelected=*/selectorIndex == 0);

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

    // Small gap under the top status bar so the first recents row is not flush
    // against it (the header ends at topPadding + homeTopPadding). Home-only:
    // this is local to the home render; the list height (below) is derived from
    // menuY - recentAreaY, so it shrinks from the top and never pushes the menu.
    constexpr int recentAreaTopGap = 8;
    int recentAreaY = metrics.topPadding + metrics.homeTopPadding + recentAreaTopGap;
    constexpr int recentAreaBottomGap = 8;

    // Over-limit warning card (selector slot 1): the first item in the recents band
    // when /sleep exceeds the rotation cap. Selecting it opens the bulk-move keypad.
    if (sleepOverLimit) {
      const int cardX = metrics.contentSidePadding;
      const int cardW = pageWidth - metrics.contentSidePadding * 2;
      const int cardH = metrics.menuRowHeight;
      const bool cardSelected = (selectorIndex == 1);
      if (cardSelected) {
        renderer.fillRect(cardX, recentAreaY, cardW, cardH, true);
      } else {
        renderer.drawRect(cardX, recentAreaY, cardW, cardH, 2, true);
      }
      const std::string warnText = std::string(tr(STR_SLEEP_OVER_LIMIT)) + ": " + std::to_string(sleepImageCount) +
                                   " / " + std::to_string(crosspoint::sleep::wallpaper::kSleepIndexMaxImages);
      const std::string shown = renderer.truncatedText(SMALL_FONT_ID, warnText.c_str(), cardW - 16);
      const int textY = recentAreaY + (cardH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
      renderer.drawText(SMALL_FONT_ID, cardX + 8, textY, shown.c_str(), !cardSelected);
      recentAreaY += cardH + 6;
    }

    const int recentAreaHeight = std::max(0, menuY - recentAreaBottomGap - recentAreaY);

    // Book rows live at selector [firstBookList(), firstBookList()+count). Pass a
    // book-based index (selectorIndex - firstBookList()); on the pages button or the
    // warning card this is negative, which highlights no row.
    const auto vis = GUI.drawRecentBookList(renderer, Rect{0, recentAreaY, pageWidth, recentAreaHeight}, recentBooks,
                                            selectorIndex - firstBookList(), scrollOffset);
    firstVisibleBookIdx = vis.firstVisible;
    lastVisibleBookIdx = vis.lastVisible;
    scrollOffset = vis.firstVisible;

    const int menuSelected = selectorIndex - firstBookList() - static_cast<int>(recentBooks.size());
    GUI.drawButtonMenu(
        renderer, Rect{0, menuY, pageWidth, menuBlockHeight}, menuCount, menuSelected,
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });

    const int bookSel = selectorIndex - firstBookList();
    const bool bookSelected =
        bookSel >= 0 && bookSel < static_cast<int>(recentBooks.size()) && !recentBooks[bookSel].path.empty();
    const char* backLabel = bookSelected ? tr(STR_REMOVE_BUTTON) : "";
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.present(pendingFullRefresh ? RefreshIntent::DeepClean : RefreshIntent::MenuNav);
    pendingFullRefresh = false;
    if (wakeMenuReadyAt_ == 0) wakeMenuReadyAt_ = millis();
    drawSleepToasts();
    return;
  }

  // ---- SINGLE_COVER home (Lector coverflow): centre book cover + peeking neighbours ----
  // Shares the LIST home's chrome (top line + bottom menu); only the centre changes.
  // Selection slots: 0 = pages tile, 1 = centre cover, 2.. = menu items. The centre
  // book (coverflowIndex) is switched with the side / page-turn buttons in loop().
  drawHomeTopLine(pageWidth, /*pagesSelected=*/selectorIndex == 0);

  const int bookCount = static_cast<int>(recentBooks.size());
  if (coverflowIndex >= bookCount) coverflowIndex = std::max(0, bookCount - 1);

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Transfer, Settings};
  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 1, Library);
  }
  const int menuCount = static_cast<int>(menuItems.size());
  const int menuBlockHeight = metrics.verticalSpacing + menuCount * metrics.menuRowHeight +
                              (menuCount > 0 ? (menuCount - 1) * metrics.menuSpacing : 0);
  constexpr int menuBottomGap = 8;
  const int menuY = pageHeight - metrics.buttonHintsHeight - menuBottomGap - menuBlockHeight;

  // Cover area fills the band between the top line and the pinned bottom menu.
  const int coverAreaY = metrics.topPadding + metrics.homeTopPadding;
  constexpr int coverAreaBottomGap = 8;
  const int coverAreaHeight = std::max(0, menuY - coverAreaBottomGap - coverAreaY);
  GUI.drawRecentBookCoverflow(renderer, Rect{0, coverAreaY, pageWidth, coverAreaHeight}, recentBooks, coverflowIndex,
                              /*coverSelected=*/selectorIndex == 1);

  const int menuSelected = selectorIndex - 2;  // slots 0 (pages) and 1 (cover) fall below the menu range
  GUI.drawButtonMenu(
      renderer, Rect{0, menuY, pageWidth, menuBlockHeight}, menuCount, menuSelected,
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const bool coverBookSelected = selectorIndex == 1 && bookCount > 0 && !recentBooks[coverflowIndex].path.empty();
  const char* backLabel = coverBookSelected ? tr(STR_REMOVE_BUTTON) : "";
  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.present(pendingFullRefresh ? RefreshIntent::DeepClean : RefreshIntent::MenuNav);
  pendingFullRefresh = false;
  if (wakeMenuReadyAt_ == 0) wakeMenuReadyAt_ = millis();
  // Toasts only once the forced first re-render is behind us: the
  // !firstRenderDone branch below immediately repaints, which would wipe a
  // toast one frame after it appeared (it is one-shot — cleared when drawn).
  if (firstRenderDone) {
    drawSleepToasts();
  }

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
  // Missing cover thumbs are generated by pumpRecentCovers from loop(), one
  // book per pass — never here, where they would block input after the paint.
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
      // pages button (+ the optional warning card), so include those when clamping.
      const bool listLayout = SETTINGS.homeLayout == CrossPointSettings::HOME_LAYOUT_LIST;
      const int pagesSlot = listLayout ? 1 : 0;
      const int menuCount = getMenuItemCount() + pagesSlot + (listLayout ? listWarnSlots() : 0);
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
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

void HomeActivity::refreshSleepOverLimit() {
  // Persisted index snapshot — no SD directory walk on the home (= wake) path.
  // The idle pump keeps it honest while the user reads; until the first-ever
  // build it reads 0, which just hides the warning card (same as today).
  // acceptEntry saturates the count AT kMaxEntries (== kSleepIndexMaxImages),
  // so the over-limit test must be >=, not > (which could never fire).
  sleepImageCount = static_cast<long>(APP_STATE.sleepIndexCount);
  sleepOverLimit = sleepImageCount >= static_cast<long>(crosspoint::sleep::wallpaper::kSleepIndexMaxImages);
}

void HomeActivity::maybeShowWallpaperPauseToast() {
  // A sleep reconcile may have demoted overflow wallpapers to /sleep pause/ while
  // the device slept. Surface that once on the next home entry, then clear the
  // persisted count so the toast does not re-fire.
  const uint16_t moved = APP_STATE.pendingSleepWallpapersMovedToPause;
  if (moved == 0) return;
  APP_STATE.pendingSleepWallpapersMovedToPause = 0;
  APP_STATE.saveToFile();
  sleepPauseToast = std::to_string(moved) + " " + tr(STR_MOVED_TO_SLEEP_PAUSE);
}

void HomeActivity::openSleepMoveKeypad() {
  // Text keyboard as a numeric entry: the user types how many wallpapers to move
  // at random from /sleep to /sleep pause. Parsed and clamped to the folder count
  // AND a per-action cap: the reservoir sampler holds all N picked names in RAM
  // (vector reserve + N strings), so an unbounded N on a ~160 KB fragmented heap
  // is a guaranteed bad_alloc abort under -fno-exceptions. 256 names ≈ 10 KB —
  // safe; bigger cleanups just take a couple of keypad rounds.
  constexpr long kMaxMovePerAction = 256;
  const long maxN = std::min(sleepImageCount, kMaxMovePerAction);
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MOVE_SLEEP_HOW_MANY),
                                                           std::string(), /*maxLength=*/5, InputType::Text);
  startActivityForResult(std::move(keyboard), [this, maxN](const ActivityResult& res) {
    if (res.isCancelled) return;
    const std::string text = std::get<KeyboardResult>(res.data).text;
    long n = 0;
    for (const char c : text) {
      if (c < '0' || c > '9') continue;
      n = n * 10 + (c - '0');
      if (n > maxN) {
        n = maxN;
        break;
      }
    }
    size_t moved = 0;
    if (n > 0) moved = crosspoint::sleep::wallpaper::moveRandomToPause(static_cast<size_t>(n));
    // Optimistic local decrement so the card updates immediately; the real count
    // lands in APP_STATE.sleepIndexCount when the idle pump rebuilds the index
    // (moveRandomToPause marked it dirty).
    sleepImageCount = (sleepImageCount >= static_cast<long>(moved)) ? sleepImageCount - static_cast<long>(moved) : 0;
    sleepOverLimit = sleepImageCount >= static_cast<long>(crosspoint::sleep::wallpaper::kSleepIndexMaxImages);
    moveToast = std::string(tr(STR_MOVED)) + " " + std::to_string(moved);
    // The warning slot may have just vanished; keep the selector in range.
    const int bookCount = static_cast<int>(recentBooks.size());
    const int navSlots = firstBookList() + bookCount + (getMenuItemCount() - bookCount);
    if (selectorIndex >= navSlots) selectorIndex = std::max(0, navSlots - 1);
    pendingFullRefresh = true;
    requestUpdate();
  });
}

void HomeActivity::drawSleepToasts() {
  // Shown over the current home frame; each is one-shot (cleared here) and the next
  // input's re-render paints the plain home again. The move result takes priority.
  if (!moveToast.empty()) {
    GUI.drawPopup(renderer, moveToast.c_str());
    moveToast.clear();
  } else if (!sleepPauseToast.empty()) {
    GUI.drawPopup(renderer, sleepPauseToast.c_str());
    sleepPauseToast.clear();
  }
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
