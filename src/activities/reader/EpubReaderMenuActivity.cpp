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
    const uint8_t paragraphNumbering, const uint8_t paperbackBody, const uint8_t paperbackStatus)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, hasReaderOverride, paragraphNumbering)),
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

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes,
                                                                                     bool hasBookmarks,
                                                                                     bool hasReaderOverride,
                                                                                     uint8_t paragraphNumbering) {
  std::vector<MenuItem> items;
  items.reserve(18);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::GRAB_QUOTE, StrId::STR_GRAB_QUOTE});
  // Per-book reader settings. "Reset" only appears once this book has its own
  // override (otherwise it already follows the global settings).
  items.push_back({MenuAction::READER_SETTINGS, StrId::STR_READER_SETTINGS});
  if (hasReaderOverride) {
    items.push_back({MenuAction::RESET_READER_SETTINGS, StrId::STR_RESET_READER_SETTINGS});
  }
  items.push_back({MenuAction::TOGGLE_PARAGRAPH_NUMBERS, StrId::STR_PARAGRAPH_NUMBERS});
  // Paperback Look toggles — in-book menu only (not in the global Settings screen).
  items.push_back({MenuAction::TOGGLE_PAPERBACK_LOOK, StrId::STR_PAPERBACK_LOOK});
  items.push_back({MenuAction::TOGGLE_PAPERBACK_STATUS, StrId::STR_PAPERBACK_STATUS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  // Jump to a paragraph number — only meaningful when this book shows paragraph
  // numbers. Toggle numbering on, reopen the menu, and this row appears.
  if (paragraphNumbering != CrossPointSettings::PARA_NUM_OFF) {
    items.push_back({MenuAction::GO_TO_PARAGRAPH, StrId::STR_GO_TO_PARAGRAPH});
  }
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return items;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
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
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
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
  const int batteryReserve = 12 + metrics.batteryWidth + renderer.getTextWidth(UI_10_FONT_ID, "100%") + 12;
  const int titleMaxWidth = screen.width - 2 * batteryReserve;
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), titleMaxWidth, 5, EpdFontFamily::BOLD);
  int y = screen.y + metrics.topPadding + 5;
  for (const auto& line : titleLines) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
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

  // Progress summary — centered: "<page>/<pages>  |  Book: <pct>%".
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::to_string(currentPage) + "/" + std::to_string(totalPages) + "  |  ";
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
      true);

  // Footer / Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
