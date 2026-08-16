#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubReaderMenuActivity::EpubReaderMenuActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title, const std::string& author,
    const std::string& chapterName, const int currentPage, const int totalPages, const int bookProgressPercent,
    const uint8_t currentOrientation, const bool hasFootnotes, const bool hasBookmarks, const bool hasReaderOverride,
    const uint8_t paragraphNumbering, const uint8_t paragraphNumberSize, const uint8_t paperbackBody,
    const uint8_t paperbackStatus, const uint8_t statusBar, const uint8_t progressBar, const bool hasSleepWallpaper,
    const bool wallpaperFavorited, const bool wallpaperPausable, const bool hasQuotes)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      tabs(buildTabs(hasFootnotes, hasBookmarks, hasReaderOverride, paragraphNumbering, statusBar, hasSleepWallpaper,
                     wallpaperFavorited, wallpaperPausable, hasQuotes)),
      title(title),
      author(author),
      chapterName(chapterName),
      pendingOrientation(currentOrientation),
      selectedParagraphNumbering(paragraphNumbering),
      selectedParagraphNumberSize(paragraphNumberSize),
      selectedPaperbackBody(paperbackBody),
      selectedPaperbackStatus(paperbackStatus),
      selectedStatusBar(statusBar),
      selectedProgressBar(progressBar),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  // Open on the wallpaper tab when there is one: this menu is normally reached by
  // waking the device, and the wallpaper rows act on the image the lock screen just
  // showed — the one thing here that is about the moment rather than the book.
  for (int i = 0; i < static_cast<int>(tabs.size()); i++) {
    if (tabs[i].tab == Tab::Sleep) {
      activeTabIndex = i;
      // Land the cursor on the favourite row rather than the tab bar, so favouriting
      // the wallpaper just shown is a single Confirm press. selectedIndex is a nav-ring
      // position: 0 is the tab bar, so row r sits at r + 1. Held in a member because
      // onEnter runs after this and sets the opening position itself.
      for (int r = 0; r < static_cast<int>(tabs[i].items.size()); r++) {
        if (tabs[i].items[r].action == MenuAction::WALLPAPER_FAVORITE) {
          openingSelectedIndex = r + 1;
          break;
        }
      }
      break;
    }
  }
}

std::vector<EpubReaderMenuActivity::TabPage> EpubReaderMenuActivity::buildTabs(
    bool hasFootnotes, bool hasBookmarks, bool hasReaderOverride, uint8_t paragraphNumbering, uint8_t statusBar,
    bool hasSleepWallpaper, bool wallpaperFavorited, bool wallpaperPausable, bool hasQuotes) {
  // Reserve every tab this menu can ever have, so no push_back below can reallocate.
  // That matters: page() hands back a reference INTO the vector, and a reallocation
  // would dangle it. Raise this with any new tab. (Each reference also dies at the end
  // of its own block, before the next page() call, so the two guards are independent.)
  std::vector<TabPage> pages;
  pages.reserve(5);
  // Each tab is appended and then filled in place, so its rows are pushed straight into
  // their final home rather than built in a temporary and copied.
  const auto page = [&pages](Tab tab, StrId label) -> std::vector<MenuItem>& {
    pages.push_back({tab, label, {}, 0});
    return pages.back().items;
  };

  // Appends a section: the heading, then its rows. A group whose rows all dropped out
  // (a conditional row that does not apply to this book) contributes no heading, so a
  // heading on screen always has something under it.
  const auto group = [](std::vector<MenuItem>& items, const StrId heading, std::vector<MenuItem> members) {
    if (members.empty()) return;
    items.push_back(MenuItem::Header(heading));
    for (auto& member : members) items.push_back(member);
  };

  // --- Navigate: everything that moves the reading position ---------------------
  {
    auto& items = page(Tab::Navigate, StrId::STR_SEC_NAVIGATE);
    std::vector<MenuItem> position{{MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER},
                                   {MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT}};
    // Jump to a paragraph number — only meaningful when this book shows paragraph
    // numbers. Toggle numbering on, reopen the menu, and this row appears.
    if (paragraphNumbering != CrossPointSettings::PARA_NUM_OFF) {
      position.push_back({MenuAction::GO_TO_PARAGRAPH, StrId::STR_GO_TO_PARAGRAPH});
    }
    group(items, StrId::STR_GRP_POSITION, std::move(position));

    std::vector<MenuItem> marks;
    if (hasBookmarks) {
      marks.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
    }
    marks.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
    if (hasFootnotes) {
      marks.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
    }
    group(items, StrId::STR_GRP_MARKS, std::move(marks));
  }

  // --- This Book: what the book says about itself, and what you take out of it ---
  {
    auto& items = page(Tab::ThisBook, StrId::STR_SEC_THIS_BOOK);
    group(items, StrId::STR_GRP_READ,
          {{MenuAction::READING_STATS, StrId::STR_READING_STATS}, {MenuAction::DICTIONARY, StrId::STR_LOOKUP}});

    std::vector<MenuItem> quotes{{MenuAction::GRAB_QUOTE, StrId::STR_GRAB_QUOTE}};
    // Reading the quotes back only makes sense once this book has a sidecar to read;
    // the caller checks for the file so this stays free of storage access.
    if (hasQuotes) {
      quotes.push_back({MenuAction::VIEW_QUOTES, StrId::STR_VIEW_QUOTES});
    }
    group(items, StrId::STR_GRP_QUOTES, std::move(quotes));

    // Undoing the open belongs with the book itself, not with the device tools, and both
    // rows sit last because they are the ones that leave the book. Deleting is the harder
    // version of removing: removing only unfiles the book, this erases the file. It asks
    // for confirmation before doing anything.
    group(items, StrId::STR_GRP_REMOVE,
          {{MenuAction::REMOVE_FROM_RECENTS, StrId::STR_REMOVE_THIS_BOOK},
           {MenuAction::DELETE_BOOK, StrId::STR_DELETE_BOOK}});
  }

  // --- Look: everything that changes how the page is drawn ----------------------
  {
    auto& items = page(Tab::Look, StrId::STR_SEC_LOOK);
    // Per-book reader settings. "Reset" only appears once this book has its own
    // override (otherwise it already follows the global settings).
    std::vector<MenuItem> thisBook{{MenuAction::READER_SETTINGS, StrId::STR_READER_SETTINGS}};
    if (hasReaderOverride) {
      thisBook.push_back({MenuAction::RESET_READER_SETTINGS, StrId::STR_RESET_READER_SETTINGS});
    }
    // Sits with the reader settings because that is what it changes: it copies another
    // book's look onto this one, once. The picker itself reports when nothing qualifies.
    thisBook.push_back({MenuAction::STEAL_LOOK, StrId::STR_STEAL_LOOK});
    // Same cluster for the same reason: a saved look applied to this book. Steal Look
    // takes one from another book, this takes one from the named sets on the card.
    thisBook.push_back({MenuAction::READING_THEMES, StrId::STR_READING_THEMES});
    group(items, StrId::STR_GRP_THIS_BOOK, std::move(thisBook));

    std::vector<MenuItem> pageGroup{{MenuAction::TOGGLE_PARAGRAPH_NUMBERS, StrId::STR_PARAGRAPH_NUMBERS}};
    // Size sits directly under the mode it belongs to, and only when there is
    // something to size. Turn numbering on, reopen the menu, and the row appears.
    if (paragraphNumbering != CrossPointSettings::PARA_NUM_OFF) {
      pageGroup.push_back({MenuAction::TOGGLE_PARAGRAPH_NUM_SIZE, StrId::STR_PARAGRAPH_NUMBER_SIZE});
    }
    pageGroup.push_back({MenuAction::TOGGLE_PAPERBACK_LOOK, StrId::STR_PAPERBACK_LOOK});
    pageGroup.push_back({MenuAction::TOGGLE_PAPERBACK_STATUS, StrId::STR_PAPERBACK_STATUS});
    group(items, StrId::STR_GRP_PAGE, std::move(pageGroup));

    items.push_back(MenuItem::Header(StrId::STR_GRP_STATUS_BAR));
    items.push_back({MenuAction::TOGGLE_STATUS_BAR, StrId::STR_STATUS_BAR});
    // Directly under the row it depends on, and only while that row reads OFF: with the
    // bar shown the progress bars already draw from Book Bar / Chapter Bar + Bar
    // Thickness, so this row would be a second control for the same thing. This decides
    // the state the menu OPENS in; syncProgressBarRow keeps it right afterwards, so the
    // row also arrives and leaves live as the Status Bar row is toggled.
    if (!statusBar) {
      items.push_back({MenuAction::TOGGLE_PROGRESS_BAR, StrId::STR_PROGRESS_BAR});
    }
    // Everything the bar can show, and where, for this book alone. Seeded from the
    // global layout the first time this book gets an override.
    items.push_back({MenuAction::CUSTOMISE_STATUS_BAR, StrId::STR_CUSTOMISE_STATUS_BAR});

    group(items, StrId::STR_GRP_SCREEN, {{MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION}});
  }

  // --- Sleep Screen: triage for the wallpaper the lock screen just showed -------
  // The whole tab is absent when the last sleep screen was not a wallpaper, or the file
  // has since left the card. "Pause" is offered only for a wallpaper in a folder it can
  // move out of.
  if (hasSleepWallpaper) {
    auto& items = page(Tab::Sleep, StrId::STR_SEC_SLEEP_SCREEN);
    items.push_back({MenuAction::WALLPAPER_FAVORITE,
                     wallpaperFavorited ? StrId::STR_UNFAVORITE_WALLPAPER : StrId::STR_FAVORITE_WALLPAPER});
    if (wallpaperPausable) {
      items.push_back({MenuAction::WALLPAPER_PAUSE, StrId::STR_PAUSE_WALLPAPER});
      // Holding only means something for a rotating folder; a fixed /sleep.pxc
      // shows the same image every night whatever this says.
      items.push_back({MenuAction::WALLPAPER_HOLD, SETTINGS.wallpaperRotationPaused
                                                       ? StrId::STR_RESUME_WALLPAPER_ROTATION
                                                       : StrId::STR_HOLD_THIS_WALLPAPER});
    }
    // Last of the wallpaper rows, because it is the destructive one and this tab is the
    // one the menu opens on — a stray press should not land on it.
    items.push_back({MenuAction::WALLPAPER_DELETE, StrId::STR_DELETE_WALLPAPER});
  }

  // --- Device: everything that is not about this book ---------------------------
  {
    auto& items = page(Tab::Device, StrId::STR_SEC_DEVICE);
    group(items, StrId::STR_GRP_TOOLS,
          {{MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON},
           {MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR},
           {MenuAction::SYNC, StrId::STR_SYNC_PROGRESS}});
    group(items, StrId::STR_GRP_STORAGE, {{MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE}});
  }

  return pages;
}

void EpubReaderMenuActivity::syncProgressBarRow() {
  for (auto& page : tabs) {
    auto& items = page.items;
    const auto bar = std::find_if(items.begin(), items.end(),
                                  [](const MenuItem& m) { return m.action == MenuAction::TOGGLE_STATUS_BAR; });
    if (bar == items.end()) continue;
    const auto next = bar + 1;
    const bool present = next != items.end() && next->action == MenuAction::TOGGLE_PROGRESS_BAR;
    if (!selectedStatusBar && !present) {
      items.insert(next, MenuItem{MenuAction::TOGGLE_PROGRESS_BAR, StrId::STR_PROGRESS_BAR});
    } else if (selectedStatusBar && present) {
      items.erase(next);
      // The cursor sits on the Status Bar row when this runs, so it is always above the
      // row being removed and does not move. Clamped anyway: a nav-ring position past
      // the end would index off the vector on the next render.
      const int last = static_cast<int>(items.size());
      if (page.selectedIndex > last) page.selectedIndex = last;
    }
    return;  // the row exists in exactly one tab
  }
}

bool EpubReaderMenuActivity::isHeaderRing(const int ringIndex) const {
  if (ringIndex <= 0) return false;  // 0 is the tab bar, never a heading
  const auto& items = activeTab().items;
  const size_t row = static_cast<size_t>(ringIndex - 1);
  return row < items.size() && items[row].isHeader;
}

int EpubReaderMenuActivity::stepPastHeaders(int ringIndex, const int direction) const {
  const int ringSize = static_cast<int>(activeTab().items.size()) + 1;
  // Bounded by the ring so a tab that somehow held nothing but headings cannot spin.
  for (int guard = 0; guard < ringSize && isHeaderRing(ringIndex); ++guard) {
    ringIndex = direction >= 0 ? ButtonNavigator::nextIndex(ringIndex, ringSize)
                               : ButtonNavigator::previousIndex(ringIndex, ringSize);
  }
  return ringIndex;
}

void EpubReaderMenuActivity::switchTab(const int direction) {
  const int count = static_cast<int>(tabs.size());
  if (count <= 1) return;
  // Choosing tabs and choosing a row are different modes, so a switch made from the tab
  // bar has to land on the tab bar. Without this, holding a nav button drops onto
  // whatever row the next tab last had selected: the bar stops drawing as focused
  // mid-hold, the Confirm hint reverts from the next tab's name to "Select", and a
  // Confirm meant as "next tab" fires that row instead — which in the Sleep tab can be
  // Delete Wallpaper, the row deliberately placed last so a stray press misses it.
  // TextSettingsActivity::switchTab does the same for the same reason.
  const bool onTabBar = activeTab().selectedIndex == 0;
  activeTabIndex = (activeTabIndex + direction + count) % count;
  if (onTabBar) {
    activeTab().selectedIndex = 0;
  } else {
    // The remembered position was landable when this tab was left, but a conditional row
    // may have come or gone since, so re-settle it off any heading it now sits on.
    activeTab().selectedIndex = stepPastHeaders(activeTab().selectedIndex, 1);
  }
  requestUpdate();
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  // Nav-ring position 0 is the tab bar, and that is normally where the menu opens: the
  // tab it opens ON carries the intent (see the constructor), so the first thing offered
  // is the choice of tab rather than whichever row happens to be first. The exception is
  // the Sleep tab's favourite row, which the constructor points at so that favouriting
  // the wallpaper just shown is a single Confirm press.
  activeTab().selectedIndex = openingSelectedIndex;
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1,
                           pendingOrientation,
                           selectedParagraphNumbering,
                           selectedParagraphNumberSize,
                           selectedPaperbackBody,
                           selectedPaperbackStatus,
                           selectedStatusBar,
                           selectedProgressBar,
                           firedHoldFunction};
  setResult(std::move(result));
  finish();
}

void EpubReaderMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing
    // release must be swallowed below (Back would close the menu, Confirm
    // would re-activate the selected item).
    popupClosing = !optionPopup.isActive();
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }

  // Menu Hold: holding Confirm anywhere in the menu runs the bound function instead of
  // activating the highlighted row. Every one of those functions needs the reader's page,
  // which this activity does not have, so the hold only reports itself and closes; the
  // reader runs the function when it takes the result back.
  //
  // Reported as CANCELLED on purpose: no row was chosen. The reader applies the live
  // toggles either way and skips the row action, which is exactly right here.
  //
  // Closing on the press (not the release) is what keeps the tap path untouched: the row
  // still activates on release, and the release that follows this hold lands in the
  // reader, whose ButtonPressLatch drops it because that press was never seen there.
  if (SETTINGS.menuHoldFunction != CrossPointSettings::LP_MENU_DISABLED) {
    // Timed from a press this activity actually SAW, never from a button that merely
    // happens to be down. Returning from a sub-screen (chapter list, confirmation) can
    // hand the menu a Confirm that is still held with the threshold already passed;
    // measuring that would fire the bound function the instant the menu came back.
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      confirmHoldStart = millis();
    }
    if (confirmHoldStart != 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      if (millis() - confirmHoldStart >= ReaderUtils::BOOKMARK_HOLD_MS) {
        firedHoldFunction = SETTINGS.menuHoldFunction;
        closeCancelled();
        return;
      }
    } else if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      confirmHoldStart = 0;
    }
  }

  // Back closes the menu and carries no hold, so it goes on the press.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeCancelled();
    return;
  }

  auto activateSelected = [this] {
    // Position 0 is the tab bar, not a row: Confirm there steps to the NEXT tab, the
    // same as holding NavNext (holding NavPrevious goes the other way; Confirm has no
    // backwards form). It stays because it is the discoverable one — the button hint
    // names the tab it moves to.
    if (activeTab().selectedIndex == 0) {
      switchTab();
      return;
    }
    const auto selectedAction = activeTab().items[activeTab().selectedIndex - 1].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       pendingOrientation, [this](int idx) {
                         pendingOrientation = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::TOGGLE_PARAGRAPH_NUMBERS) {
      // Cycle Off / Per Chapter in place; applied by the reader on exit.
      selectedParagraphNumbering = (selectedParagraphNumbering + 1) % CrossPointSettings::PARAGRAPH_NUMBERING_COUNT;
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::TOGGLE_PARAGRAPH_NUM_SIZE) {
      // Cycle Small / Double in place; applied by the reader on exit, like the row above.
      selectedParagraphNumberSize = (selectedParagraphNumberSize + 1) % CrossPointSettings::PARAGRAPH_NUMBER_SIZE_COUNT;
      requestUpdate();
      return;
    }

    // Paperback Look toggles: flip in place (like the rows above) and keep the menu
    // open so the ON/OFF label updates like a checkbox; the reader applies them on exit.
    if (selectedAction == MenuAction::TOGGLE_PAPERBACK_LOOK) {
      selectedPaperbackBody = selectedPaperbackBody ? 0 : 1;
      requestUpdate();
      return;
    }
    if (selectedAction == MenuAction::TOGGLE_PAPERBACK_STATUS) {
      selectedPaperbackStatus = selectedPaperbackStatus ? 0 : 1;
      requestUpdate();
      return;
    }
    if (selectedAction == MenuAction::TOGGLE_STATUS_BAR) {
      selectedStatusBar = selectedStatusBar ? 0 : 1;
      // The Progress Bar row belongs to the OFF state, so it arrives and leaves with
      // this toggle rather than waiting for the menu to be reopened.
      syncProgressBarRow();
      requestUpdate();
      return;
    }
    if (selectedAction == MenuAction::TOGGLE_PROGRESS_BAR) {
      // Cycle Off / Slim / Medium / Fat in place; the reader applies it on exit.
      selectedProgressBar = (selectedProgressBar + 1) % CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT;
      requestUpdate();
      return;
    }

    // Favouriting is handed to the reader like every other action, and the reader queues
    // the card work rather than waiting for it. Nothing happens here, deliberately: an
    // in-place label flip costs a menu redraw, and leaving the menu already costs a page
    // repaint, so doing both pays two panel refreshes for one press. Falling straight
    // through pays one. The row label is right the next time the menu opens because the
    // reader moves APP_STATE to the new name before it returns.

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedParagraphNumbering,
                         selectedParagraphNumberSize, selectedPaperbackBody, selectedPaperbackStatus, selectedStatusBar,
                         selectedProgressBar, firedHoldFunction});
    finish();
  };

  // Handle navigation. The ring is the tab bar at 0 followed by this tab's rows, so
  // walking off either end of the list lands back on the tab bar. Same gesture split the
  // other tabbed screens use (TextSettingsActivity, SettingsActivity): a tap moves one
  // step along the ring, and holding switches tab instead of repeating. The hold has to
  // carry tabs because NavNext/NavPrevious already carry Left/Right on the same axis as
  // Down/Up, so there is no spare direction to spend. Moving on release (not press) is
  // what makes the two separable: ButtonNavigator::onRelease suppresses itself once a
  // hold has fired, so a hold switches tabs without also stepping the cursor when the
  // button comes back up.
  //
  // The order below is load-bearing. The release handlers must run before the continuous
  // ones so a tap's step is applied before anything in the same pass can raise the
  // suppression flag. Reversed, a continuous that fires first sets the flag and the
  // release is swallowed — and that is reachable, because a logical direction covers two
  // physical buttons (NavNext is Down or Right), so one can still be held while the
  // other is released in the same pass.
  const int ringSize = static_cast<int>(activeTab().items.size()) + 1;

  buttonNavigator.onNextStep([this, ringSize] {
    activeTab().selectedIndex = stepPastHeaders(ButtonNavigator::nextIndex(activeTab().selectedIndex, ringSize), 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousStep([this, ringSize] {
    activeTab().selectedIndex =
        stepPastHeaders(ButtonNavigator::previousIndex(activeTab().selectedIndex, ringSize), -1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] { switchTab(); });
  buttonNavigator.onPreviousContinuous([this] { switchTab(-1); });

  // With a function bound to the menu hold, Confirm carries two actions and cannot be
  // resolved until the button comes up. With nothing bound there is nothing to tell
  // apart, so the row activates the instant Confirm goes down.
  const bool confirmHasHold = SETTINGS.menuHoldFunction != CrossPointSettings::LP_MENU_DISABLED;
  if (confirmHasHold ? mappedInput.wasReleased(MappedInputManager::Button::Confirm)
                     : mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Battery cluster only (top-right); the title is drawn (wrapped) below so it can span
  // as many lines as it needs, followed by the author, chapter, and progress lines.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight}, nullptr);

  // Wrap the book title over multiple centered lines. Reserve space on both sides
  // symmetrically so the first line never runs under the battery cluster.
  const int batteryReserve = BaseTheme::batteryClusterWidth(renderer) + 12;
  const int titleMaxWidth = screen.width - 2 * batteryReserve;
  // Same size and weight as the author and chapter lines below: at UI_12 bold the
  // title read as a heavy slab against the thin lines under it.
  const int titleLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, title.c_str(), titleMaxWidth, 5, EpdFontFamily::REGULAR);
  int y = screen.y + metrics.topPadding + 5;
  for (const auto& line : titleLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::REGULAR);
    y += titleLineHeight;
  }
  y += 2;

  const int subLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 2;

  // "by {author}" — centered, only when an author is known.
  if (!author.empty()) {
    const std::string byLine = std::string(tr(STR_BY_PREFIX)) + author;
    const std::string truncatedByLine =
        renderer.truncatedText(UI_10_FONT_ID, byLine.c_str(), screen.width - 40, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, y, truncatedByLine.c_str());
    y += subLineHeight;
  }

  // Current chapter name — centered.
  if (!chapterName.empty()) {
    const std::string truncatedChapter =
        renderer.truncatedText(UI_10_FONT_ID, chapterName.c_str(), screen.width - 40, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, y, truncatedChapter.c_str());
    y += subLineHeight;
  }

  // Progress summary — centered: "Pages: <page>/<pages>  |  Book: <pct>%". Both halves
  // carry a label so neither reads as a bare number.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine =
        std::string(tr(STR_PAGES_PREFIX)) + std::to_string(currentPage) + "/" + std::to_string(totalPages) + "  |  ";
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, y, progressLine.c_str());
  y += subLineHeight;

  // Tab bar sits between the book header and the rows. It is nav-ring position 0, so it
  // draws as selected whenever the cursor is on it.
  const int tabTop = y + metrics.verticalSpacing;
  const bool onTabBar = activeTab().selectedIndex == 0;
  std::vector<TabInfo> tabInfos;
  tabInfos.reserve(tabs.size());
  for (int t = 0; t < static_cast<int>(tabs.size()); t++) {
    tabInfos.push_back({I18N.get(tabs[t].labelId), t == activeTabIndex});
  }
  GUI.drawTabBar(renderer, Rect{screen.x, tabTop, screen.width, metrics.tabBarHeight}, tabInfos, onTabBar);

  const int contentTop = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  // The list draws only this tab's rows. selectedIndex is a ring position, so it is
  // shifted down by one to index the rows, and -1 (no row selected) parks the highlight
  // while the tab bar has focus.
  const auto& items = activeTab().items;
  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, items.size(),
      onTabBar ? -1 : activeTab().selectedIndex - 1,
      [this](int index) { return I18N.get(activeTab().items[index].labelId); }, nullptr, nullptr,
      [this](int index) {
        const auto value = activeTab().items[index].action;
        if (value == MenuAction::ROTATE_SCREEN) {
          // Render current orientation value on the right edge of the content area.
          return I18N.get(orientationLabels[pendingOrientation]);
        } else if (value == MenuAction::TOGGLE_PARAGRAPH_NUMBERS) {
          // Render current paragraph-numbering mode on the right edge.
          return I18N.get(paragraphNumLabels[selectedParagraphNumbering % paragraphNumLabels.size()]);
        } else if (value == MenuAction::TOGGLE_PARAGRAPH_NUM_SIZE) {
          return I18N.get(paragraphNumSizeLabels[selectedParagraphNumberSize % paragraphNumSizeLabels.size()]);
        } else if (value == MenuAction::TOGGLE_PAPERBACK_LOOK) {
          return I18N.get(selectedPaperbackBody ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else if (value == MenuAction::TOGGLE_PAPERBACK_STATUS) {
          return I18N.get(selectedPaperbackStatus ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else if (value == MenuAction::TOGGLE_STATUS_BAR) {
          return I18N.get(selectedStatusBar ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else if (value == MenuAction::TOGGLE_PROGRESS_BAR) {
          return I18N.get(progressBarLabels[selectedProgressBar % progressBarLabels.size()]);
        } else {
          return "";
        }
      },
      true, nullptr, UI_10_FONT_ID, [this](int index) { return activeTab().items[index].isHeader; });

  // Footer / Hints. On the tab bar the Confirm button moves to the next tab, so the hint
  // names that tab rather than saying "Select" — that is what makes tab switching
  // findable without knowing the hold gesture.
  const char* confirmLabel = tr(STR_SELECT);
  if (onTabBar && tabs.size() > 1) {
    confirmLabel = I18N.get(tabs[(activeTabIndex + 1) % static_cast<int>(tabs.size())].labelId);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
