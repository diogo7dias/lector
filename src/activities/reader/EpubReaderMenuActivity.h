#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderMenuActivity final : public Activity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    BOOK_INFO,
    READER_SETTINGS,        // open this book's per-book reader settings
    RESET_READER_SETTINGS,  // clear this book's override, follow global again
    STEAL_LOOK,             // copy another book's reader settings onto this book
    FOOTNOTES,
    GO_TO_PERCENT,
    GO_TO_PARAGRAPH,  // jump to a paragraph number within the current chapter
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    TOGGLE_PAPERBACK_LOOK,
    TOGGLE_PAPERBACK_STATUS,
    TOGGLE_PARAGRAPH_NUMBERS,  // cycle off / per-chapter / whole-book (per book)
    TOGGLE_RANDOM_ON_BOOT,
    SCREENSHOT,
    DISPLAY_QR,
    SHARE_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    HIGHLIGHT_QUOTE,
    VIEW_QUOTES,
    READING_STATS,
    // Sleep-wallpaper triage (only shown when a last-shown wallpaper exists).
    WALLPAPER_FAVORITE,
    WALLPAPER_PAUSE_ROTATION,
    WALLPAPER_MOVE_PAUSE,
    WALLPAPER_DELETE
  };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const std::string& author, const std::string& chapterName, const int currentPage,
                                  const int totalPages, const int bookProgressPercent, const uint8_t currentOrientation,
                                  const bool hasFootnotes, bool hasBookmarks, bool hasQuotes,
                                  bool hasSleepWallpaper = false, bool wallpaperPaused = false,
                                  bool wallpaperFavorited = false, bool hasReaderOverride = false,
                                  uint8_t paperbackBody = 1, uint8_t paperbackStatus = 1,
                                  uint8_t paragraphNumbering = 0);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool hasFootnotes, bool hasBookmarks, bool hasQuotes,
                                              bool hasSleepWallpaper, bool wallpaperPaused, bool wallpaperFavorited,
                                              bool hasReaderOverride, uint8_t paragraphNumbering);

  // Fixed menu layout
  const std::vector<MenuItem> menuItems;

  int selectedIndex = 0;

  ButtonNavigator buttonNavigator;
  std::string title = "Reader Menu";
  std::string author;
  std::string chapterName;
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  // Per-book Paperback Look, toggled live in the menu; returned via MenuResult.
  uint8_t selectedPaperbackBody = 1;
  uint8_t selectedPaperbackStatus = 1;
  // Per-book paragraph numbering, cycled live in the menu; returned via MenuResult.
  uint8_t selectedParagraphNumbering = 0;
  const std::vector<StrId> paragraphNumLabels = {StrId::STR_PARA_NUM_OFF, StrId::STR_PARA_NUM_CHAPTER,
                                                 StrId::STR_PARA_NUM_BOOK};
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
