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
    STEAL_LOOK,                 // copy another book's reader settings onto this one
    READING_THEMES,             // saved reader looks: apply one to this book, or save this one
    WALLPAPER_FAVORITE,         // star/unstar the wallpaper the lock screen last showed
    WALLPAPER_PAUSE,            // move that wallpaper to "/sleep pause", out of rotation
    WALLPAPER_HOLD,             // stop picking a new wallpaper each sleep; keep this one
    WALLPAPER_DELETE,           // delete that wallpaper file from the card, behind a confirmation
    REMOVE_FROM_RECENTS,        // drop this book from the home list and put its file back at the card root
    DELETE_BOOK,                // erase this book's file and its cache from the card, behind a confirmation
    RETURN,                     // back to the last deliberate-jump origin
    VIEW_QUOTES                 // browse (and delete) the quotes saved in <book>_QUOTES.txt
  };

  // Tab pages of the menu. Sleep exists only when the lock screen last showed a
  // wallpaper that is still on the card, so the live tab list can be shorter than
  // this enum — see buildTabs().
  enum class Tab : uint8_t { Navigate, ThisBook, Look, Sleep, Device };

  // What the reader hands the menu, by name. The constructor used to take 23 positional
  // arguments, eleven of them adjacent bools and uint8_ts where a swap compiled silently.
  struct Context {
    std::string title;
    std::string author;
    std::string chapterName;
    int currentPage = 0;
    int totalPages = 0;
    int bookProgressPercent = 0;
    uint8_t currentOrientation = 0;
    bool hasFootnotes = false;
    bool hasBookmarks = false;
    bool hasReaderOverride = false;
    uint8_t paragraphNumbering = 0;
    uint8_t paragraphNumberSize = 1;
    uint8_t paperbackBody = 1;
    uint8_t paperbackStatus = 1;
    uint8_t statusBar = 1;
    uint8_t progressBar = 0;
    bool hasSleepWallpaper = false;
    bool wallpaperFavorited = false;
    bool wallpaperPausable = false;
    bool hasQuotes = false;
    bool hasReturn = false;
  };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Context& context);

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return sections.visibleCount(rows.data(), static_cast<int>(rows.size())); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowAction(const freeink::ui::ActionEvent& event) override { activateIndex(event.value); }
  // The book block above the list is chrome, not rows: title, author, chapter and
  // progress. The base reserves the band it paints, so the two cannot drift.
  ListChrome chrome() const override;
  // Popup input, and the Confirm hold that runs the bound menu function. Both own the
  // pass before the base looks at Back, Confirm or the selection.
  bool handleCustomInput() override;
  // Back collapses or closes on the press; Confirm activates on press or release depending on
  // whether a menu hold function is bound. Neither matches the base defaults.
  bool handleButtons() override;
  // A press walks visible rows; a hold jumps to the next section header.
  void navigateButtons() override;
  bool drawOverlay() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
    // Landable section heading, drawn as an inverted band by the SDK.
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

  // Builds only the tabs that have something to show, so indices into the result are
  // NOT Tab values and the Sleep tab simply is not there when no wallpaper is in play.
  static std::vector<TabPage> buildTabs(const Context& context);
  // The result every close hands back: the live toggles plus the chosen action.
  MenuResult resultFor(int action) const;
  // Adds or removes the Progress Bar row to match selectedStatusBar, in place, so the
  // row appears the moment the Status Bar row is switched off rather than on the next
  // menu open. Called under the render lock; the focused header and Status Bar
  // row are before the insertion/removal, so their indexes stay valid.
  void syncProgressBarRow();
  // Flattens the built sections into the one list the menu shows: each section's label
  // becomes a heading row, followed by that section's rows.
  static std::vector<MenuItem> flatten(const std::vector<TabPage>& pages);
  // Refresh borrowed labels/values in the storage reserved on entry.
  void updateRows();
  void focusRow(int index);
  void closeCancelled();
  // The block's strings, held so the ListChrome can borrow them. Mutable because
  // chrome() is const: rebuilding the block changes nothing about the screen.
  mutable std::vector<std::string> headerBlock;
  // The value column text for a row, or nullptr when the row carries no value.
  const char* rowValue(int index) const;

  // Row values own nothing; the labels come from I18N and the values from fixed label
  // tables, so the ListItems can borrow both.
  std::vector<freeink::ui::ListItem> rows;

  // Full menu data; the SDK maps visible indexes without rebuilding this list.
  std::vector<MenuItem> items;

  // One transient index; never part of settings, MenuResult or web state.
  freeink::ui::ListSections sections;

  OptionPopup optionPopup;
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
