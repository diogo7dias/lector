#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubReaderMenuActivity::EpubReaderMenuActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title, const std::string& author,
    const std::string& chapterName, const int currentPage, const int totalPages, const int bookProgressPercent,
    const uint8_t currentOrientation, const bool hasFootnotes, const bool hasBookmarks, const bool hasReaderOverride,
    const uint8_t paragraphNumbering, const uint8_t paperbackBody, const uint8_t paperbackStatus,
    const bool hasSleepWallpaper, const bool wallpaperFavorited, const bool wallpaperPausable, const bool hasQuotes)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, hasReaderOverride, paragraphNumbering, hasSleepWallpaper,
                               wallpaperFavorited, wallpaperPausable, hasQuotes)),
      title(title),
      author(author),
      chapterName(chapterName),
      pendingOrientation(currentOrientation),
      selectedParagraphNumbering(paragraphNumbering),
      selectedPaperbackBody(paperbackBody),
      selectedPaperbackStatus(paperbackStatus),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(
    bool hasFootnotes, bool hasBookmarks, bool hasReaderOverride, uint8_t paragraphNumbering, bool hasSleepWallpaper,
    bool wallpaperFavorited, bool wallpaperPausable, bool hasQuotes) {
  std::vector<MenuItem> items;
  items.reserve(32);
  const auto section = [&items](StrId label) { items.push_back({MenuAction::SECTION, label}); };

  // --- Navigate: everything that moves the reading position ---------------------
  section(StrId::STR_SEC_NAVIGATE);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  // Jump to a paragraph number — only meaningful when this book shows paragraph
  // numbers. Toggle numbering on, reopen the menu, and this row appears.
  if (paragraphNumbering != CrossPointSettings::PARA_NUM_OFF) {
    items.push_back({MenuAction::GO_TO_PARAGRAPH, StrId::STR_GO_TO_PARAGRAPH});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }

  // --- This Book: what the book says about itself, and what you take out of it ---
  section(StrId::STR_SEC_THIS_BOOK);
  items.push_back({MenuAction::BOOK_INFO, StrId::STR_BOOK_INFO});
  items.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::GRAB_QUOTE, StrId::STR_GRAB_QUOTE});
  // Reading the quotes back only makes sense once this book has a sidecar to read;
  // the caller checks for the file so this stays free of storage access.
  if (hasQuotes) {
    items.push_back({MenuAction::VIEW_QUOTES, StrId::STR_VIEW_QUOTES});
  }
  // Undoing the open belongs with the book itself, not with the device tools, and it
  // sits last here because it is the one row that leaves the book.
  items.push_back({MenuAction::REMOVE_FROM_RECENTS, StrId::STR_REMOVE_THIS_BOOK});

  // --- Look: everything that changes how the page is drawn ----------------------
  section(StrId::STR_SEC_LOOK);
  // Per-book reader settings. "Reset" only appears once this book has its own
  // override (otherwise it already follows the global settings).
  items.push_back({MenuAction::READER_SETTINGS, StrId::STR_READER_SETTINGS});
  if (hasReaderOverride) {
    items.push_back({MenuAction::RESET_READER_SETTINGS, StrId::STR_RESET_READER_SETTINGS});
  }
  // Sits with the reader settings because that is what it changes: it copies another
  // book's look onto this one, once. The picker itself reports when nothing qualifies.
  items.push_back({MenuAction::STEAL_LOOK, StrId::STR_STEAL_LOOK});
  // Same cluster for the same reason: a saved look applied to this book. Steal Look
  // takes one from another book, this takes one from the named sets on the card.
  items.push_back({MenuAction::READING_THEMES, StrId::STR_READING_THEMES});
  items.push_back({MenuAction::TOGGLE_PARAGRAPH_NUMBERS, StrId::STR_PARAGRAPH_NUMBERS});
  items.push_back({MenuAction::TOGGLE_PAPERBACK_LOOK, StrId::STR_PAPERBACK_LOOK});
  items.push_back({MenuAction::TOGGLE_PAPERBACK_STATUS, StrId::STR_PAPERBACK_STATUS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});

  // --- Sleep Screen: triage for the wallpaper the lock screen just showed -------
  // Absent when the last sleep screen was not a wallpaper, or the file has since left
  // the card. "Pause" is offered only for a wallpaper in a folder it can move out of.
  if (hasSleepWallpaper) {
    section(StrId::STR_SEC_SLEEP_SCREEN);
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
    // Last of the wallpaper rows, because it is the destructive one and this menu is
    // reached by waking the device — a stray press should not land on it.
    items.push_back({MenuAction::WALLPAPER_DELETE, StrId::STR_DELETE_WALLPAPER});
  }

  // --- Device: everything that is not about this book ---------------------------
  section(StrId::STR_SEC_DEVICE);
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  return items;
}

int EpubReaderMenuActivity::firstSelectableFrom(int index, const bool forward) const {
  const int count = static_cast<int>(menuItems.size());
  if (count == 0) return 0;
  // Bounded by count so a list that somehow held only headings cannot spin forever.
  for (int steps = 0; steps < count; steps++) {
    if (!isSection(index)) return index;
    index = forward ? ButtonNavigator::nextIndex(index, count) : ButtonNavigator::previousIndex(index, count);
  }
  return index;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  // Index 0 is the first section heading, so open on the first real option under it.
  selectedIndex = firstSelectableFrom(0, true);
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1,
                           pendingOrientation,
                           selectedPageTurnOption,
                           selectedParagraphNumbering,
                           selectedPaperbackBody,
                           selectedPaperbackStatus};
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return;
  }

  auto activateSelected = [this] {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::SECTION) return;  // heading row: nothing to activate
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       pendingOrientation, [this](int idx) {
                         pendingOrientation = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
      optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                       static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                         selectedPageTurnOption = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::TOGGLE_PARAGRAPH_NUMBERS) {
      // Cycle Off / Per Chapter / Whole Book in place; applied by the reader on exit.
      selectedParagraphNumbering = (selectedParagraphNumbering + 1) % CrossPointSettings::PARAGRAPH_NUMBERING_COUNT;
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

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption,
                         selectedParagraphNumbering, selectedPaperbackBody, selectedPaperbackStatus});
    finish();
  };

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex =
        firstSelectableFrom(ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size())), true);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex =
        firstSelectableFrom(ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size())), false);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
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

  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, menuItems.size(), selectedIndex,
      [this](int index) { return I18N.get(menuItems[index].labelId); }, nullptr, nullptr,
      [this](int index) {
        const auto value = menuItems[index].action;
        if (value == MenuAction::ROTATE_SCREEN) {
          // Render current orientation value on the right edge of the content area.
          return I18N.get(orientationLabels[pendingOrientation]);
        } else if (value == MenuAction::AUTO_PAGE_TURN) {
          // Render current page turn value on the right edge of the content area.
          return pageTurnLabels[selectedPageTurnOption];
        } else if (value == MenuAction::TOGGLE_PARAGRAPH_NUMBERS) {
          // Render current paragraph-numbering mode on the right edge.
          return I18N.get(paragraphNumLabels[selectedParagraphNumbering % paragraphNumLabels.size()]);
        } else if (value == MenuAction::TOGGLE_PAPERBACK_LOOK) {
          return I18N.get(selectedPaperbackBody ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else if (value == MenuAction::TOGGLE_PAPERBACK_STATUS) {
          return I18N.get(selectedPaperbackStatus ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else {
          return "";
        }
      },
      true, nullptr, UI_10_FONT_ID, [this](int index) { return isSection(index); });

  // Footer / Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
