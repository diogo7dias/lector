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
      items(flatten(buildTabs(hasFootnotes, hasBookmarks, hasReaderOverride, paragraphNumbering, statusBar,
                              hasSleepWallpaper, wallpaperFavorited, wallpaperPausable, hasQuotes))),
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
  // The menu is one list, so the setting that used to pick a tab now picks the section
  // the list opens on. A section with nothing to show is simply not in the list, and
  // the lookup falls back to the first row.
  preferredTab = tabForSetting(SETTINGS.bookMenuTab);
}

// The heading each section is built with, so a lookup by label can find it again in
// the flattened list. Kept beside buildTabs: the two must name the same strings.
static StrId labelForTab(const EpubReaderMenuActivity::Tab tab) {
  switch (tab) {
    case EpubReaderMenuActivity::Tab::ThisBook:
      return StrId::STR_SEC_THIS_BOOK;
    case EpubReaderMenuActivity::Tab::Look:
      return StrId::STR_SEC_LOOK;
    case EpubReaderMenuActivity::Tab::Sleep:
      return StrId::STR_SEC_SLEEP_SCREEN;
    case EpubReaderMenuActivity::Tab::Device:
      return StrId::STR_SEC_DEVICE;
    case EpubReaderMenuActivity::Tab::Navigate:
    default:
      return StrId::STR_SEC_NAVIGATE;
  }
}

// Section label first, then that section's rows. A section that built no rows
// contributes nothing at all, heading included.
std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::flatten(const std::vector<TabPage>& pages) {
  std::vector<MenuItem> flat;
  for (const auto& page : pages) {
    if (page.items.empty()) continue;
    flat.push_back(MenuItem::Header(page.labelId));
    flat.insert(flat.end(), page.items.begin(), page.items.end());
  }
  return flat;
}

EpubReaderMenuActivity::Tab EpubReaderMenuActivity::tabForSetting(const uint8_t setting) {
  switch (setting) {
    case CrossPointSettings::BOOK_MENU_TAB_THIS_BOOK:
      return Tab::ThisBook;
    case CrossPointSettings::BOOK_MENU_TAB_LOOK:
      return Tab::Look;
    case CrossPointSettings::BOOK_MENU_TAB_DEVICE:
      return Tab::Device;
    case CrossPointSettings::BOOK_MENU_TAB_SLEEP:
      return Tab::Sleep;
    default:
      return Tab::Navigate;
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

  // Sections run in the order a reader reaches for them. Look first: type size, the page
  // and the status bar are what gets changed mid-book, and this menu is one tap away from
  // the page. Then Navigate, then the rows about the book itself, then the wallpaper
  // triage, then the device tools nobody opens a book to find.
  //
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
          {{MenuAction::READING_STATS, StrId::STR_READING_STATS},
           {MenuAction::DICTIONARY, StrId::STR_LOOKUP},
           // Recall sits next to the lookup it records, because that is where a reader
           // goes looking for a word they have already met.
           {MenuAction::DICTIONARY_HISTORY, StrId::STR_LOOKUP_HISTORY}});

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
           {MenuAction::SYNC, StrId::STR_SYNC_PROGRESS},
           {MenuAction::NEARBY_SYNC, StrId::STR_NEARBY_SYNC},
           {MenuAction::NEARBY_SEND_BOOK, StrId::STR_NEARBY_SEND_FILE}});
    group(items, StrId::STR_GRP_STORAGE, {{MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE}});
  }

  return pages;
}

void EpubReaderMenuActivity::syncProgressBarRow() {
  const auto bar = std::find_if(items.begin(), items.end(),
                                [](const MenuItem& m) { return m.action == MenuAction::TOGGLE_STATUS_BAR; });
  if (bar == items.end()) return;
  const auto next = bar + 1;
  const bool present = next != items.end() && next->action == MenuAction::TOGGLE_PROGRESS_BAR;
  if (!selectedStatusBar && !present) {
    items.insert(next, MenuItem{MenuAction::TOGGLE_PROGRESS_BAR, StrId::STR_PROGRESS_BAR});
  } else if (selectedStatusBar && present) {
    items.erase(next);
    // The cursor sits on the Status Bar row when this runs, so it is always above the
    // row being removed and does not move. Clamped anyway: a position past the end would
    // index off the vector on the next render.
    const int last = static_cast<int>(items.size()) - 1;
    if (selectedIndex > last) selectedIndex = std::max(0, last);
  }
}

bool EpubReaderMenuActivity::isHeaderRow(const int index) const {
  const size_t row = static_cast<size_t>(index);
  return index >= 0 && row < items.size() && items[row].isHeader;
}

int EpubReaderMenuActivity::stepPastHeaders(int index, const int direction) const {
  const int count = static_cast<int>(items.size());
  if (count <= 0) return 0;
  // Bounded by the list so one made of nothing but headings cannot spin.
  for (int guard = 0; guard < count && isHeaderRow(index); ++guard) {
    index = direction >= 0 ? ButtonNavigator::nextIndex(index, count) : ButtonNavigator::previousIndex(index, count);
  }
  return index;
}

void EpubReaderMenuActivity::jumpSection(const bool forward) {
  const int count = static_cast<int>(items.size());
  if (count <= 0) return;
  // Walk to the next heading in that direction, then land on the row under it. Parking
  // the window on the heading keeps the section's name on screen, so a jump reads as
  // arriving somewhere rather than as the list sliding by an arbitrary amount.
  int index = selectedIndex;
  for (int guard = 0; guard < count; ++guard) {
    index = forward ? ButtonNavigator::nextIndex(index, count) : ButtonNavigator::previousIndex(index, count);
    if (isHeaderRow(index)) {
      listScrollOffset = std::max(0, index);
      selectedIndex = stepPastHeaders(index, 1);
      requestUpdate();
      return;
    }
  }
}

int EpubReaderMenuActivity::firstRowOfPreferredSection() const {
  // Headings carry no Tab value, so the section is found by its label: buildTabs gives
  // each section the heading its own labelId names.
  const StrId wanted = labelForTab(preferredTab);
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    if (items[i].isHeader && items[i].labelId == wanted) return stepPastHeaders(i, 1);
  }
  return stepPastHeaders(0, 1);
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  // Opens on the first row of the section the user chose in Settings, with that
  // section's heading at the top of the window so the list reads from its name down.
  selectedIndex = firstRowOfPreferredSection();
  listScrollOffset = std::max(0, selectedIndex - 1);
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
    // Headings are never selectable, so anything landed on here is a real row.
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()) || isHeaderRow(selectedIndex)) return;
    const auto selectedAction = items[selectedIndex].action;
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

  // A tap on a row selects and activates it in one go, the same as the Settings list.
  int tappedRow = 0;
  if (mappedInput.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow < static_cast<int>(items.size()) &&
      !isHeaderRow(tappedRow)) {
    selectedIndex = tappedRow;
    activateSelected();
    return;
  }

  // Handle navigation. One flat list of headings and rows, walked exactly the way the
  // Settings list is: a press steps one row past any heading, and holding jumps to the
  // next section instead of repeating — the fast travel the tab bar used to provide.
  const int count = static_cast<int>(items.size());

  buttonNavigator.onNextStep([this, count] {
    selectedIndex = stepPastHeaders(ButtonNavigator::nextIndex(selectedIndex, count), 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousStep([this, count] {
    selectedIndex = stepPastHeaders(ButtonNavigator::previousIndex(selectedIndex, count), -1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] { jumpSection(true); });
  buttonNavigator.onPreviousContinuous([this] { jumpSection(false); });

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

  const int contentTop = y + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  // One list of headings and rows, scrolled rather than paged so the rows around the
  // cursor hold still as it moves.
  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, items.size(), selectedIndex,
      [this](int index) { return I18N.get(items[index].labelId); }, nullptr, nullptr,
      [this](int index) {
        const auto value = items[index].action;
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
      true, nullptr, UI_10_FONT_ID, [this](int index) { return items[index].isHeader; }, &listScrollOffset);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
