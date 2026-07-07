#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const std::string& author,
                                               const std::string& chapterName, const int currentPage,
                                               const int totalPages, const int bookProgressPercent,
                                               const uint8_t currentOrientation, const bool hasFootnotes,
                                               const bool hasBookmarks, const bool hasQuotes, bool hasSleepWallpaper,
                                               bool wallpaperPaused, bool wallpaperFavorited)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, hasQuotes, hasSleepWallpaper, wallpaperPaused,
                               wallpaperFavorited)),
      title(title),
      author(author),
      chapterName(chapterName),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes,
                                                                                     bool hasBookmarks, bool hasQuotes,
                                                                                     bool hasSleepWallpaper,
                                                                                     bool wallpaperPaused,
                                                                                     bool wallpaperFavorited) {
  std::vector<MenuItem> items;
  items.reserve(15);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::HIGHLIGHT_QUOTE, StrId::STR_GRAB_QUOTE});
  if (hasQuotes) {
    items.push_back({MenuAction::VIEW_QUOTES, StrId::STR_VIEW_QUOTES});
  }
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  // Toggle Bookmark intentionally hidden from the in-book menu — a bookmark can
  // still be dropped via the long-press Confirm function (SETTINGS.longPressMenuFunction).
  // Paperback Look toggles — in-book pop-up menu only (not in global Settings/web).
  items.push_back({MenuAction::TOGGLE_PAPERBACK_LOOK, StrId::STR_PAPERBACK_LOOK});
  items.push_back({MenuAction::TOGGLE_PAPERBACK_STATUS, StrId::STR_PAPERBACK_STATUS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  // Auto Turn (Pages Per Minute) intentionally hidden from the in-book menu.
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  // "Reset Pages Read" moved to the home screen's pages button (Confirm resets).

  // Sleep-wallpaper triage — only when the device knows which wallpaper was last
  // shown (so the actions have a concrete target). Labels reflect current state.
  if (hasSleepWallpaper) {
    items.push_back({MenuAction::WALLPAPER_FAVORITE, wallpaperFavorited ? StrId::STR_UNFAVORITE : StrId::STR_FAVORITE});
    items.push_back(
        {MenuAction::WALLPAPER_PAUSE_ROTATION, wallpaperPaused ? StrId::STR_TRIAGE_UNPAUSE : StrId::STR_TRIAGE_PAUSE});
    items.push_back({MenuAction::WALLPAPER_MOVE_PAUSE, StrId::STR_MOVE_TO_SLEEP_PAUSE});
    items.push_back({MenuAction::WALLPAPER_DELETE, StrId::STR_TRIAGE_DELETE});
  }
  return items;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::loop() {
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
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      // Cycle orientation preview locally; actual rotation happens on menu exit.
      pendingOrientation = (pendingOrientation + 1) % orientationLabels.size();
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
      selectedPageTurnOption = (selectedPageTurnOption + 1) % pageTurnLabels.size();
      requestUpdate();
      return;
    }

    // Paperback Look toggles: flip in place (like the orientation/auto-turn rows
    // above), persist immediately, and keep the menu open so the ON/OFF label
    // updates like a checkbox. The reader auto-re-renders on menu close and picks
    // up the new SETTINGS value, so the ink weight changes as soon as you exit.
    if (selectedAction == MenuAction::TOGGLE_PAPERBACK_LOOK) {
      SETTINGS.paperbackLookBody = SETTINGS.paperbackLookBody ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      return;
    }
    if (selectedAction == MenuAction::TOGGLE_PAPERBACK_STATUS) {
      SETTINGS.paperbackLookStatus = SETTINGS.paperbackLookStatus ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
    finish();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
    setResult(std::move(result));
    finish();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Battery cluster only (top-right); the title is drawn (wrapped) below so it can
  // span as many lines as it needs instead of being truncated to one.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight}, nullptr);

  // Wrap the book title over multiple centered lines. Reserve space on both sides
  // symmetrically so the first line never runs under the battery cluster (mirrors
  // the battery geometry drawHeader uses on the right edge).
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
    const std::string byLine = std::string(tr(STR_BY_AUTHOR_PREFIX)) + author;
    const std::string truncatedByLine =
        renderer.truncatedText(UI_10_FONT_ID, byLine.c_str(), screen.width - 40, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, y, truncatedByLine.c_str());
    y += subLineHeight;
  }

  // Current chapter name — centered (falls back to "Unnamed" at the launch site).
  if (!chapterName.empty()) {
    const std::string truncatedChapter =
        renderer.truncatedText(UI_10_FONT_ID, chapterName.c_str(), screen.width - 40, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, y, truncatedChapter.c_str());
    y += subLineHeight;
  }

  // Progress summary — centered, same font/size as the chapter line:
  // "<page>/<pages>  |  Book: <pct>%".
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
        } else if (value == MenuAction::TOGGLE_PAPERBACK_LOOK) {
          // Show current ON/OFF state so the row reads like a checkbox.
          return I18N.get(SETTINGS.paperbackLookBody ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
        } else if (value == MenuAction::TOGGLE_PAPERBACK_STATUS) {
          return I18N.get(SETTINGS.paperbackLookStatus ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
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
