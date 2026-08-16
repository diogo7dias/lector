#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class EpubReaderMenuActivity final : public Activity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SECTION_HEADER,  // not an action: the marker a section heading row carries
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    GO_TO_PERCENT,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    SYNC,
    TOGGLE_KOSYNC_AUTO,  // turn unattended two-way KOReader sync on or off, in place
    DELETE_CACHE,
    DICTIONARY,
    READER_SETTINGS,            // open this book's per-book reader settings
    RESET_READER_SETTINGS,      // clear this book's override, follow global again
    TOGGLE_PARAGRAPH_NUMBERS,   // cycle off / per-chapter / whole-book in place
    TOGGLE_PARAGRAPH_NUM_SIZE,  // cycle Small / Double in place
    TOGGLE_PAPERBACK_LOOK,      // toggle heavier ink for reader body text
    TOGGLE_PAPERBACK_STATUS,    // toggle heavier ink for status bar text
    TOGGLE_STATUS_BAR,          // show or hide the reading status bar for this book only
    TOGGLE_PROGRESS_BAR,        // cycle Off / Slim / Medium / Fat for the bar that outlives a hidden status bar
    CUSTOMISE_STATUS_BAR,       // open the full per-item status bar screen for this book
    GO_TO_PARAGRAPH,            // jump to a paragraph number (only when numbering is on)
    GRAB_QUOTE,                 // pick a passage on the page and save it to <book>_QUOTES.txt
    READING_STATS,              // per-book and all-books reading statistics
    STEAL_LOOK,                 // copy another book's reader settings onto this one
    READING_THEMES,             // saved reader looks: apply one to this book, or save this one
    WALLPAPER_FAVORITE,         // star/unstar the wallpaper the lock screen last showed
    WALLPAPER_PAUSE,            // move that wallpaper to "/sleep pause", out of rotation
    WALLPAPER_HOLD,             // stop picking a new wallpaper each sleep; keep this one
    WALLPAPER_DELETE,           // delete that wallpaper file from the card, behind a confirmation
    REMOVE_FROM_RECENTS,        // drop this book from the home list and put its file back at the card root
    DELETE_BOOK,                // erase this book's file and its cache from the card, behind a confirmation
    VIEW_QUOTES                 // browse (and delete) the quotes saved in <book>_QUOTES.txt
  };

  // Tab pages of the menu. Sleep exists only when the lock screen last showed a
  // wallpaper that is still on the card, so the live tab list can be shorter than
  // this enum — see buildTabs().
  enum class Tab : uint8_t { Navigate, ThisBook, Look, Sleep, Device };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const std::string& author, const std::string& chapterName, const int currentPage,
                                  const int totalPages, const int bookProgressPercent, const uint8_t currentOrientation,
                                  const bool hasFootnotes, bool hasBookmarks, bool hasReaderOverride = false,
                                  uint8_t paragraphNumbering = 0, uint8_t paragraphNumberSize = 1,
                                  uint8_t paperbackBody = 1, uint8_t paperbackStatus = 1, uint8_t statusBar = 1,
                                  uint8_t progressBar = 0, bool hasSleepWallpaper = false,
                                  bool wallpaperFavorited = false, bool wallpaperPausable = false,
                                  bool hasQuotes = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
    // Section heading rather than a row: drawn as a filled bar, never landable. The
    // nav ring steps past it, so no handler ever sees SECTION_HEADER.
    bool isHeader = false;

    static MenuItem Header(const StrId labelId) { return MenuItem{MenuAction::SECTION_HEADER, labelId, true}; }
  };

  // One tab page: which tab it is, the label drawn in the tab bar, its rows, and where
  // the cursor sits inside it. selectedIndex is a nav-ring position, not a row index:
  // 0 is the tab bar itself, 1..N are the rows. Each tab keeps its own cursor, so
  // leaving a tab and coming back lands where you left it.
  struct TabPage {
    Tab tab;
    StrId labelId;
    std::vector<MenuItem> items;
    int selectedIndex = 0;
  };

  // Builds only the tabs that have something to show, so indices into the result are
  // NOT Tab values and the Sleep tab simply is not there when no wallpaper is in play.
  static std::vector<TabPage> buildTabs(bool hasFootnotes, bool hasBookmarks, bool hasReaderOverride,
                                        uint8_t paragraphNumbering, uint8_t statusBar, bool hasSleepWallpaper,
                                        bool wallpaperFavorited, bool wallpaperPausable, bool hasQuotes);
  // Adds or removes the Progress Bar row to match selectedStatusBar, in place, so the
  // row appears the moment the Status Bar row is switched off rather than on the next
  // menu open. Safe to call from loop(): it mutates one tab's item vector, never the
  // tabs vector itself, and no reference into either outlives the call.
  void syncProgressBarRow();
  TabPage& activeTab() { return tabs[activeTabIndex]; }
  const TabPage& activeTab() const { return tabs[activeTabIndex]; }
  // Move to another tab, wrapping. The cursor of the tab being left is kept.
  // True when this nav-ring position is a section heading (ring 0 is the tab bar).
  bool isHeaderRing(int ringIndex) const;
  // Walks on in `direction` until the ring position is landable, so a heading is never
  // selected and Confirm can never fire on one.
  int stepPastHeaders(int ringIndex, int direction) const;
  void switchTab(int direction = 1);
  void closeCancelled();

  // Fixed menu layout: rows never change after construction, only the cursors do.
  std::vector<TabPage> tabs;

  int activeTabIndex = 0;
  // Nav-ring position the menu opens on. 0 (the tab bar) unless the constructor found a
  // row worth pointing straight at; onEnter applies it, so it must outlive the constructor.
  int openingSelectedIndex = 0;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  // True while the button press that closed the popup is still held; its release
  // must not fall through to the menu's own Back/Confirm handlers.
  bool popupClosing = false;
  std::string title = "Reader Menu";
  std::string author;
  std::string chapterName;
  uint8_t pendingOrientation = 0;
  uint8_t selectedParagraphNumbering = 0;
  uint8_t selectedParagraphNumberSize = 1;
  // Per-book Paperback Look, toggled live in the menu; returned via MenuResult.
  uint8_t selectedPaperbackBody = 1;
  uint8_t selectedPaperbackStatus = 1;
  uint8_t selectedStatusBar = 1;
  // Global Progress Bar value, cycled in the menu; returned via MenuResult like the rest.
  uint8_t selectedProgressBar = 0;
  // Set when Confirm was held long enough to fire the bound Menu Hold function.
  // Reported to the reader, which owns the page the function needs. LP_MENU_DISABLED
  // (1) when no hold fired — 0 would mean KOSync.
  uint8_t firedHoldFunction = CrossPointSettings::LP_MENU_DISABLED;
  unsigned long confirmHoldStart = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<StrId> paragraphNumLabels = {StrId::STR_PARA_NUM_OFF, StrId::STR_PARA_NUM_CHAPTER};
  const std::vector<StrId> paragraphNumSizeLabels = {StrId::STR_PARA_NUM_SIZE_SMALL, StrId::STR_PARA_NUM_SIZE_DOUBLE};
  // Same four labels the Customise Status Bar screen uses for this setting.
  const std::vector<StrId> progressBarLabels = {StrId::STR_STATE_OFF, StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM,
                                                StrId::STR_FAT};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
