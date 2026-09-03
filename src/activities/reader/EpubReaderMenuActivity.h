#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class EpubReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SECTION_HEADER,  // not an action: the marker a section heading row carries
    SELECT_CHAPTER,
    FOOTNOTES,
    GO_TO_PERCENT,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    SYNC,
    NEARBY_SYNC,       // trade the reading position with another reader over ESP-NOW
    NEARBY_SEND_BOOK,  // send this book itself to another reader over ESP-NOW
    DELETE_CACHE,
    DICTIONARY,
    DICTIONARY_HISTORY,         // the words looked up before, newest first
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

 protected:
  int listCount() const override { return static_cast<int>(items.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The book block above the list is chrome, not rows: title, author, chapter and
  // progress. The base reserves the band it paints, so the two cannot drift.
  ListChrome chrome() const override;
  // Popup input, and the Confirm hold that runs the bound menu function. Both own the
  // pass before the base looks at Back, Confirm or the selection.
  bool handleCustomInput() override;
  // Back closes on the press; Confirm activates on press or release depending on
  // whether a menu hold function is bound. Neither matches the base defaults.
  bool handleButtons() override;
  // A press steps past headings; a hold jumps to the next section instead of repeating.
  void navigateButtons() override;
  bool drawOverlay() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
    // Section heading rather than a row: drawn as a filled bar, never landable. The
    // nav ring steps past it, so no handler ever sees SECTION_HEADER.
    bool isHeader = false;

    static MenuItem Header(const StrId labelId) { return MenuItem{MenuAction::SECTION_HEADER, labelId, true}; }
  };

  // One section of the menu: what it is, the heading drawn above its rows, and the rows
  // themselves. Sections are built separately and then flattened into the single list
  // the menu shows, so a section that has nothing to offer simply contributes nothing.
  struct TabPage {
    Tab tab;
    StrId labelId;
    std::vector<MenuItem> items;
    int selectedIndex = 0;
  };

  // Maps a CrossPointSettings::BOOK_MENU_TAB value onto the tab it names. Unknown
  // values fall back to Navigate, so a setting written by a newer firmware cannot
  // leave the menu pointing at nothing.
  static Tab tabForSetting(uint8_t setting);
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
  // Flattens the built sections into the one list the menu shows: each section's label
  // becomes a heading row, followed by that section's rows.
  static std::vector<MenuItem> flatten(const std::vector<TabPage>& pages);
  // True when this row is a section heading.
  bool isHeaderRow(int index) const;
  // Walks on in `direction` until the row is landable, so a heading is never selected
  // and Confirm can never fire on one.
  int stepPastHeaders(int index, int direction) const;
  // Jumps the cursor to the next or previous section heading's first row, the same fast
  // travel the Settings list gives a held nav button.
  void jumpSection(bool forward);
  // Index of the first landable row of the section named by SETTINGS.bookMenuTab, or 0.
  int firstRowOfPreferredSection() const;
  void closeCancelled();
  // The wrapped title lines. The book block itself is chrome() headerLines, so
  // the base reserves exactly what it draws.
  std::vector<std::string> titleLines() const;
  // The block's strings, held so the ListChrome can borrow them. Mutable because
  // chrome() is const: rebuilding the block changes nothing about the screen.
  mutable std::vector<std::string> headerBlock;
  // The value column text for a row, or nullptr when the row carries no value.
  const char* rowValue(int index) const;

  // Row values own nothing; the labels come from I18N and the values from fixed label
  // tables, so the ListItems can borrow both.
  std::vector<freeink::ui::ListItem> rows;

  // Fixed menu layout: one flat list of headings and rows, built once. Only the cursor
  // and the scroll window move after that.
  std::vector<MenuItem> items;
  std::vector<TabPage> pages;
  size_t currentTabIdx = 0;
  bool onTabBar = false;
  mutable std::vector<struct TabInfo> tabInfos;
  void updateTabInfos() const;
  void selectTab(size_t index);

  // The section the menu opened on, kept so the constructor's choice survives onEnter.
  Tab preferredTab = Tab::Navigate;

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
