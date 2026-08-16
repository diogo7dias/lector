#pragma once
#include <ArduinoJson.h>
#include <Epub/ReaderRenderSpec.h>
#include <PersistableStore.h>

#include <cstdint>

#include "activities/reader/ReaderPrefs.h"

// The whole status bar configuration as one value, so it can be swapped in and out
// atomically while a book with its own bar is open.
struct StatusBarBlock {
  uint8_t enabled = 1;
  uint8_t batteryPos = 0;
  uint8_t clockPos = 0;
  uint8_t titlePos = 0;
  uint8_t titleSource = 0;
  uint8_t titleTruncate = 0;
  uint8_t pagePos = 0;
  uint8_t pageFormat = 0;
  uint8_t bookPctPos = 0;
  uint8_t chapterPctPos = 0;
  uint8_t chapterNumPos = 0;
  uint8_t sessionPagesPos = 0;
  uint8_t bookBar = 0;
  uint8_t chapterBar = 0;
  uint8_t barThickness = 0;
  uint8_t floatingBar = 0;
  uint8_t barOutline = 0;
  uint8_t offBar = 0;
};

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  friend class PersistableStore<CrossPointSettings>;

 public:
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    COVER_CUSTOM = 4,
    // RETIRED: no longer offered in Settings, and fromJson() migrates a stored 5 to DARK.
    // The slot stays so every value after it keeps its meaning; renderBlankSleepScreen()
    // and its switch case stay too, so re-offering it is a one-line change.
    BLANK = 5,
    QUICK_RESUME = 6,
    // Appended at the end: the stored value is the persisted setting, so new faces
    // must never be inserted before an existing one.
    STATS_DASHBOARD = 7,
    // RETIRED (2026-08-11, Diogo): kept the last reader page and drew a frame around it,
    // except the frame never appeared on the device. All of its behaviour is deleted —
    // the renderer, the frame-colour setting, and its branch in enterDeepSleep. The slot
    // stays reserved so the values around it keep their meaning, and fromJson() migrates
    // a stored 8 to DARK. Quick Resume on Timeout covers the "keep my page" case.
    FREEZE = 8,
    // Draws an alpha overlay over whatever the panel is already holding, instead of
    // replacing it. Upstream (#2937) gave this slot 7; here 7 is already STATS_DASHBOARD,
    // so it is appended after FREEZE — the stored value is the persisted setting.
    TRANSPARENT_CUSTOM = 9,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };

  // Action for a short Back press on the home menu, where Back has no navigation target.
  // What a short Back press does on the home screen, where Back has no navigation target.
  // STATS opens the Reading Stats screen for the most recent book, read straight off the
  // card — the home screen has no reading session, so the numbers are whatever the last
  // reading session saved.
  enum HOME_BACK_ACTION { HOME_BACK_NONE = 0, HOME_BACK_RESUME = 1, HOME_BACK_STATS = 2, HOME_BACK_ACTION_COUNT };
  enum AUTHOR_DISPLAY { AUTHOR_INITIALS = 0, AUTHOR_FULL_NAME = 1, AUTHOR_DISPLAY_COUNT };

  // Status bar: the legacy fixed-slot enums (STATUS_BAR_PROGRESS_BAR / _THICKNESS /
  // _TITLE / _CLOCK_MODE) were removed with the v1 renderer. XTC keeps its own mode.
  enum XTC_STATUS_BAR_MODE {
    XTC_STATUS_BAR_HIDE = 0,
    XTC_STATUS_BAR_BOTTOM = 1,
    XTC_STATUS_BAR_TOP = 2,
    XTC_STATUS_BAR_MODE_COUNT
  };

  // --- Per-item status bar model (v2). Each text item is parked at one of six
  // anchors (or Off). This is the only status-bar model. ---
  enum STATUS_BAR_ANCHOR {
    SB_ANCHOR_OFF = 0,
    SB_ANCHOR_TL = 1,  // top-left
    SB_ANCHOR_TC = 2,  // top-center
    SB_ANCHOR_TR = 3,  // top-right
    SB_ANCHOR_BL = 4,  // bottom-left
    SB_ANCHOR_BC = 5,  // bottom-center
    SB_ANCHOR_BR = 6,  // bottom-right
    STATUS_BAR_ANCHOR_COUNT
  };
  enum STATUS_BAR_TITLE_SOURCE { SB_TITLE_BOOK = 0, SB_TITLE_CHAPTER = 1, STATUS_BAR_TITLE_SOURCE_COUNT };
  enum STATUS_BAR_PAGE_FORMAT {
    SB_PAGE_FRACTION = 0,  // "3/40"
    SB_PAGE_LEFT = 1,      // "8 left"
    STATUS_BAR_PAGE_FORMAT_COUNT
  };
  enum STATUS_BAR_EDGE { SB_EDGE_OFF = 0, SB_EDGE_TOP = 1, SB_EDGE_BOTTOM = 2, STATUS_BAR_EDGE_COUNT };
  enum STATUS_BAR_BAR_THICKNESS { SB_BAR_SLIM = 0, SB_BAR_MEDIUM = 1, SB_BAR_FAT = 2, STATUS_BAR_BAR_THICKNESS_COUNT };
  // Progress bar kept alive while the status bar itself is hidden. Off plus the same
  // three thicknesses, so one row is both the switch and the thickness. Values 1..3
  // map onto STATUS_BAR_BAR_THICKNESS 0..2.
  enum STATUS_BAR_OFF_BAR {
    SB_OFFBAR_OFF = 0,
    SB_OFFBAR_SLIM = 1,
    SB_OFFBAR_MEDIUM = 2,
    SB_OFFBAR_FAT = 3,
    STATUS_BAR_OFF_BAR_COUNT
  };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Up = Previous, Down = Next
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, SIDE_BUTTON_LAYOUT_COUNT };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName).
  // Vollkorn is the sole built-in reading family; more fonts are added from the SD card.
  enum FONT_FAMILY { VOLLKORN = 0, FONT_FAMILY_COUNT };
  static constexpr uint8_t LEGACY_OPENDYSLEXIC = 2;
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Reader font size is a point size, not an enum slot — see fontPointSize.
  // Legacy 1.4-and-earlier files stored a 0..3 SMALL/MEDIUM/LARGE/EXTRA_LARGE
  // slot; fromJson() folds that range up (see LEGACY_FONT_SIZE_MAX).
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = 3;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    // Appended last so saved indices keep their meaning. getRefreshFrequency()
    // reports 0 for this, which every caller reads as "never force a refresh".
    REFRESH_NEVER = 5,
    REFRESH_FREQUENCY_COUNT
  };

  enum REFRESH_ACTION { REFRESH_ACTION_FULL = 0, REFRESH_ACTION_BW_REINFORCEMENT = 1, REFRESH_ACTION_COUNT };

  // Short power button press actions
  enum SHORT_PWRBTN { IGNORE = 0, SLEEP = 1, PAGE_TURN = 2, FORCE_REFRESH = 3, FOOTNOTES = 4, SHORT_PWRBTN_COUNT };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by index: any new function (e.g. dictionary, bookmark) MUST use a
  // value >= 2 and be appended at the END of the enumValues array in SettingsList.h, otherwise the
  // stored indices shift and existing saves are silently misinterpreted.
  enum LONG_PRESS_MENU_FUNCTION {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_GRAB_QUOTE = 4,
    // Appended for the shared binding list (0.20). Order is frozen: the values
    // below double as bit positions in popupItems, so reordering them would both
    // shift saved bindings and silently re-tick a different set of pop-up rows.
    // RETIRED 2026-08-11 (Diogo): both left the offered list. The values stay so every
    // binding value after them keeps its meaning, they are listed in withHiddenEnumValues()
    // in SettingsList.h, and fromJson folds a binding still set to one of them to Disabled.
    LP_MENU_SELECT_CHAPTER = 5,
    LP_MENU_GO_TO_PERCENT = 6,
    LP_MENU_GO_TO_PARAGRAPH = 7,
    LP_MENU_FOOTNOTES = 8,
    LP_MENU_TEXT_SETTINGS = 9,
    LP_MENU_READER_SETTINGS = 10,
    LP_MENU_TOGGLE_STATUS_BAR = 11,
    // Not an action: opens the pop-up built from popupItems. Always last, and it is
    // the only value runBoundMenuFunction() refuses to run, so a pop-up cannot list
    // itself and recurse.
    LP_MENU_POPUP = 12,
    // Holds the wallpaper the lock screen last showed, instead of picking a new one at
    // the next sleep. Appended after LP_MENU_POPUP, so Menu Pop-up is no longer the last
    // value even though it is still the only non-action one.
    LP_MENU_WALLPAPER_HOLD = 13,
    // The list of saved bookmarks, next to the toggle that adds one.
    LP_MENU_BOOKMARKS = 14,
    // The saved-quotes viewer, next to Grab Quote that writes to it.
    LP_MENU_VIEW_QUOTES = 15,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Actions that may be ticked into the pop-up: every binding value except Disabled
  // and Menu Pop-up itself. Used by the tick screen and by the pop-up builder, so
  // both agree on the row order without either owning a second list.
  static constexpr uint8_t POPUP_ITEM_FUNCTIONS[] = {
      LP_MENU_KOSYNC,        LP_MENU_BOOKMARK,        LP_MENU_BOOKMARKS,         LP_MENU_DICTIONARY,
      LP_MENU_GRAB_QUOTE,    LP_MENU_VIEW_QUOTES,     LP_MENU_GO_TO_PARAGRAPH,   LP_MENU_FOOTNOTES,
      LP_MENU_TEXT_SETTINGS, LP_MENU_READER_SETTINGS, LP_MENU_TOGGLE_STATUS_BAR, LP_MENU_WALLPAPER_HOLD};

  // Cap on ticked pop-up rows. Every action can be ticked at once; the ceiling only exists
  // because popupItems is a 16-bit mask, so 16 is as many bits as there are to set. The
  // pop-up itself no longer needs the cap to stay on screen — option_popup::compute()
  // tightens its spacing when the rows would otherwise run off the panel.
  static constexpr uint8_t POPUP_ITEM_MAX = 16;

  // UI Theme
  // Lector ships a single UI theme (the CrossPoint "Classic" base, renamed). All
  // lector UI/look customization lives in BaseTheme; the multi-theme picker was
  // removed. Kept as an enum so uiTheme storage + UITheme::setTheme stay unchanged.
  enum UI_THEME { LECTOR = 0 };

  // Image rendering in EPUB reader
  // Images are always drawn. PLACEHOLDER (alt text only) and SUPPRESS were retired
  // 2026-08-11 (Diogo) and the Settings row went with them.
  //
  // The values, the ReaderRenderSpec field and the parser branches that read it all stay.
  // spec.imageRendering is serialised into every cached section and compared on load, so
  // deleting it would change the section file format and force every book on the card to
  // re-index. Both spec builders now hard-set IMAGES_DISPLAY instead, which reaches stored
  // per-book overrides and reader presets as well as the global value — and which makes a
  // section cached under the old placeholder layout mismatch and rebuild, but only for the
  // books that actually used it.
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // Paragraph numbering mode. The value is per-book (ReaderPrefs::paragraphNumbering);
  // this enum is only the shared value type. Numbers are drawn in the left margin at
  // render time from an ordinal already baked into the page cache (no reflow), so
  // switching is instant and needs no cache rebuild.
  // Whole-book numbering (value 2) was removed 2026-08-11 (Diogo). It could only learn a
  // chapter's paragraph count by DRAWING that chapter, so an unread chapter counted as
  // zero and every number shifted as more of the book was read — a number written down
  // one day pointed at a different paragraph the next. Counting properly would mean
  // indexing the whole book up front, which is exactly what this reader avoids.
  // A stored 2 is clamped to PARA_NUM_CHAPTER on load.
  enum PARAGRAPH_NUMBERING {
    PARA_NUM_OFF = 0,
    PARA_NUM_CHAPTER = 1,  // resets to 1 at each chapter
    PARAGRAPH_NUMBERING_COUNT
  };

  // How large those paragraph numbers are drawn. The numbers use a bitmap face, which
  // is only exact on whole multiples of its own cell, so the choice is deliberately two
  // fixed steps rather than a free size: Small is the native 12px cell (8px digits) and
  // Double is that same cell at 2x (16px digits). Per-book, like the mode above.
  enum PARAGRAPH_NUMBER_SIZE { PARA_NUM_SIZE_SMALL = 0, PARA_NUM_SIZE_DOUBLE = 1, PARAGRAPH_NUMBER_SIZE_COUNT };

  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Night mode: inverted output polarity on the reading surfaces only
  // (resolved per render by ActivityManager via Activity::appliesNightMode).
  uint8_t screenInverted = 0;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Draw the wallpaper's filename in the bottom-left corner of the sleep screen,
  // with the _F favorite suffix stripped and favorites shown as "[F] name".
  uint8_t showSleepImageFilename = 0;
  // Draw a small "F" badge instead, for favorites only. Ignored when
  // showSleepImageFilename is on, which already marks favorites.
  uint8_t showSleepFavoriteBadge = 0;
  // Draw "position / total" with a progress bar in the bottom-right corner of
  // the sleep screen, showing how far the wallpaper rotation is through its
  // current loop. Only drawn for index picks — the jump-pick fallback has no
  // position to report.
  uint8_t showSleepWallpaperPosition = 0;
  // Hold the current wallpaper instead of picking a new one at every sleep.
  // Applies only to the /sleep folder rotation; a fixed /sleep.pxc or /sleep.bmp
  // never rotates in the first place.
  //
  // No longer a Display row: it is reached from the in-book menu and from any binding set
  // to Hold Wallpaper, both of which act on the wallpaper the lock screen actually showed.
  // With no SettingsList entry the generic loop cannot carry it, so it is saved and loaded
  // by hand in toJson/fromJson.
  uint8_t wallpaperRotationPaused = 0;
  // Status bar (per-item v2 model). Legacy fixed-slot fields were removed. XTC keeps its own overlay.
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // Master on/off. NEVER read this field to decide whether to draw or reserve the bar —
  // call statusBarEnabled() instead, so a book that turned the bar off for itself is
  // honoured. This field stays the global default and the value written to settings.json.
  uint8_t sbEnabled = 1;
  uint8_t sbBatteryPos = SB_ANCHOR_BL;        // battery anchor
  uint8_t sbClockPos = SB_ANCHOR_OFF;         // clock anchor (X3 RTC only)
  uint8_t sbTitlePos = SB_ANCHOR_BC;          // title anchor
  uint8_t sbTitleSource = SB_TITLE_CHAPTER;   // book or chapter title
  uint8_t sbTitleTruncate = 0;                // 0 = greedy, no ellipsis (drives reflow); 1 = clip with ellipsis
  uint8_t sbPagePos = SB_ANCHOR_BR;           // page-in-chapter anchor
  uint8_t sbPageFormat = SB_PAGE_FRACTION;    // "3/40" vs "8 left"
  uint8_t sbBookPctPos = SB_ANCHOR_BR;        // book % (B:NN%) anchor
  uint8_t sbChapterPctPos = SB_ANCHOR_OFF;    // chapter % (C:NN%) anchor
  uint8_t sbChapterNumPos = SB_ANCHOR_OFF;    // chapter #/total (Ch N/M) anchor
  uint8_t sbSessionPagesPos = SB_ANCHOR_OFF;  // pages turned this sitting (+N) anchor
  uint8_t sbBookBar = SB_EDGE_OFF;            // book progress bar edge (Off/Top/Bottom)
  uint8_t sbChapterBar = SB_EDGE_OFF;         // chapter progress bar edge
  uint8_t sbBarThickness = SB_BAR_MEDIUM;     // progress bar thickness slim/med/fat
  // Lift the progress bars off the screen edge: one small margin applied to the
  // outer edge and to both ends, so the bar reads as a floating pill instead of
  // a strip welded to the frame. Position and thickness are unaffected.
  uint8_t sbFloatingBar = 0;
  // Outline the full length of the progress bar so the unfilled part of the
  // track stays visible. Independent of sbFloatingBar; all four combinations
  // are valid.
  uint8_t sbBarOutline = 0;
  // Off / Slim / Medium / Fat. Only consulted while the status bar is hidden, where it
  // keeps the configured Book Bar / Chapter Bar edges drawing at its own thickness.
  // Off (the default) is the old behaviour: hiding the bar hides its progress bars too.
  uint8_t sbOffBar = SB_OFFBAR_OFF;
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  // Quarter-hour granularity supports oddball zones like Nepal (+5:45) and Chatham (+12:45).
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  // Reader paragraph spacing as a percentage of the line height (block gap between
  // paragraphs; 0 = off). Restored granular control (old lector). Feeds the render
  // spec, so a change rebuilds the section cache.
  static constexpr uint8_t MAX_PARAGRAPH_SPACING = 150;
  // Half a line of air between paragraphs by default: with the first-line indent below
  // it, a paragraph break is visible at a glance rather than inferred from the ragged
  // right edge of the line above.
  static constexpr uint8_t DEFAULT_PARAGRAPH_SPACING = reader_defaults::PARAGRAPH_SPACING_PERCENT;
  uint8_t paragraphSpacing = DEFAULT_PARAGRAPH_SPACING;
  // Off by default, as in the old fork. The grayscale text pass is imperceptible on
  // this panel but costs a fading grey refresh on every page turn, which is very
  // perceptible. The toggle is kept so it can still be tried; only the default moved.
  uint8_t textAntiAliasing = 0;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonFollowOrientation = 0;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = VOLLKORN;
  // Point size of the reader font (upstream #2720 replaced the SMALL/MEDIUM/LARGE
  // enum with a real point size). Only sizes the active family actually ships are
  // selectable; SdCardFontSystem::ensureLoaded() snaps this to the nearest
  // available size (and persists the snap) whenever the family changes.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  // Legacy coarse line-spacing enum (TIGHT/NORMAL/WIDE). Superseded by
  // lineSpacingPercent below; retained so old saves still load and existing
  // references stay valid. resolveLineCompression now reads the percent.
  uint8_t lineSpacing = NORMAL;
  // Reader line spacing as a percentage of the font's natural line height (100 =
  // natural). Restored granular control (old lector). The resolved line-compression
  // float is part of the cache key, so a change rebuilds the section cache.
  static constexpr uint8_t MIN_LINE_SPACING_PERCENT = 35;
  static constexpr uint8_t MAX_LINE_SPACING_PERCENT = 150;
  uint8_t lineSpacingPercent = 100;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  // Periodic screen-maintenance action. Required cleanup refreshes remain full.
  uint8_t refreshAction = REFRESH_ACTION_FULL;
  uint8_t hyphenationEnabled = 0;

  // Reader screen margins. screenMargin is the horizontal (left/right) margin and,
  // when uniformMargins is on, also drives top/bottom. With uniformMargins off,
  // screenMarginTop/Bottom take over the vertical margins independently. Restored
  // granular range (old lector). Margins feed the viewport, so a change rebuilds
  // the section cache through the viewport dimensions (no cache-format bump needed).
  static constexpr uint8_t SCREEN_MARGIN_MIN = 0;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 100;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 1;
  uint8_t screenMargin = 5;
  uint8_t screenMarginTop = 5;
  uint8_t screenMarginBottom = 5;
  uint8_t uniformMargins = 1;  // 1 = all sides use screenMargin; 0 = separate H / Top / Bottom
  // Auto-widen horizontal margins toward ~62 chars/line (0 = off, 1 = auto min 10px,
  // 2 = auto min 20px). Overrides the fixed horizontal margin when on. Feeds the
  // viewport width, so a change re-paginates.
  static constexpr uint8_t DYNAMIC_MARGINS_COUNT = 3;
  uint8_t dynamicMargins = 0;

  // First-line paragraph indent (restored old-lector model). BOOK = respect the
  // publisher/CSS indent; PERCENT = a custom indent as a percentage of the column
  // width (0% = flush, 100% = start at the column's horizontal middle). Feeds the
  // render spec, so a change rebuilds the section cache like any layout setting.
  enum FIRST_LINE_INDENT_MODE : uint8_t { FIRST_LINE_INDENT_BOOK = 0, FIRST_LINE_INDENT_PERCENT = 1 };
  static constexpr uint8_t MAX_FIRST_LINE_INDENT_PERCENT = 100;
  // Default to a real indent rather than trusting the publisher's CSS, which on many
  // EPUBs is absent entirely.
  static constexpr uint8_t DEFAULT_FIRST_LINE_INDENT_PERCENT = reader_defaults::FIRST_LINE_INDENT_PERCENT;
  uint8_t firstLineIndentMode = FIRST_LINE_INDENT_PERCENT;
  uint8_t firstLineIndentPercent = DEFAULT_FIRST_LINE_INDENT_PERCENT;
  // OPDS download destination folder ("" = SD root). Global; edited from the
  // OPDS server list. Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen.
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Persisted via a category-less SettingInfo::Enum,
  // edited from the OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Defaults to Disabled so shortcut-based bookmark toggling remains opt-in.
  uint8_t longPressMenuFunction = LP_MENU_DISABLED;
  // Hold-Confirm function INSIDE the in-book menu. Shares the LONG_PRESS_MENU_FUNCTION
  // values so both bindings offer the same list. Held Confirm closes the menu and hands
  // the choice back to the reader, which owns the page every one of those functions
  // needs. Costs nothing when Disabled, and nothing when set: the menu acts on release,
  // so a plain tap is unaffected either way.
  uint8_t menuHoldFunction = LP_MENU_DISABLED;
  // Double-click of the power button while reading an EPUB. Shares the same
  // LONG_PRESS_MENU_FUNCTION list as the two bindings above.
  //
  // Unlike those two this one is not free when set: nothing can tell a single click
  // from the first half of a double click until the window closes, so arming it delays
  // every single power click by DoubleClickDetector::WINDOW_MS. That cost is confined
  // to the EPUB reader (main.cpp only arms the detector there) and disappears entirely
  // at LP_MENU_DISABLED, which is why Disabled is the default.
  uint8_t doubleClickPowerFunction = LP_MENU_DISABLED;
  // Which actions the Menu Pop-up lists, as a bitmask indexed by LONG_PRESS_MENU_FUNCTION
  // value (bit 2 = Toggle Bookmark, and so on). A mask rather than a list so the row order
  // is always POPUP_ITEM_FUNCTIONS order and can never drift from the tick screen.
  // At most POPUP_ITEM_MAX bits are ever set; the tick screen enforces it.
  uint16_t popupItems = 0;
  // UI Theme
  uint8_t uiTheme = LECTOR;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // Guide Dots — draw a middle dot (U+00B7) between words as a reading aid. Restored
  // (old lector). Feeds the render spec (changes word width), so it rebuilds the cache.
  uint8_t guideDotsEnabled = 0;
  // Diagnostic: outline the reader text viewport (0 = off, 1 = on). Drawn as an
  // overlay after the page renders, so it never affects layout or the cache.
  uint8_t debugBorders = 0;
  // Custom text for the wake/unlock screen bottom banner (restored old lector).
  // Empty = the default displayed string "READ UNTIL YOU DIE." (not stored here).
  char customFooter[64] = "";
  // How this reader introduces itself to another reader over Nearby Position Sync,
  // so a sync screen can say whose page it is offering rather than showing a MAC
  // address. Empty = the generated fallback from getEffectiveDeviceName().
  // The wire format caps a name at 20 bytes; the extra slack is for the NUL and
  // for a stored name being truncated at send time rather than at entry.
  char deviceName[24] = "";
  // Paperback Look: smear drawn glyph pixels +1px right/+1px down for heavier ink.
  // Two independent toggles, both default ON: body = reader page text (EPUB/TXT/XTC),
  // status = the reading-screen status bar. The global values are the default that
  // per-book ReaderPrefs seed from; the EPUB reader then uses its per-book copy, the
  // TXT/XTC readers use these global values directly.
  uint8_t paperbackLookBody = 1;
  uint8_t paperbackLookStatus = 1;
  // Default paragraph numbering for books that have no per-book override yet. A book
  // already carrying its own reader_override.bin keeps whatever it was set to in the
  // in-book menu; this only seeds the next book opened fresh.
  uint8_t paragraphNumbering = PARA_NUM_CHAPTER;
  // Default size for those numbers. Double is the default: at the native cell the digits
  // are 8px tall, which reads as too small on the device.
  uint8_t paragraphNumberSize = PARA_NUM_SIZE_DOUBLE;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // TXT reader font, kept apart from the EPUB reader font above. A plain text file
  // is usually a dump (a log, a scrape, a note) rather than a typeset book, so it
  // reads best packed small; the default is deliberately the smallest built-in size
  // and it never follows the EPUB font. Empty txtSdFontFamilyName = built-in family.
  static constexpr uint8_t TXT_DEFAULT_FONT_POINT_SIZE = 12;
  uint8_t txtFontPointSize = TXT_DEFAULT_FONT_POINT_SIZE;
  char txtSdFontFamilyName[32] = "";
  // Dictionary folder name under /dictionaries (empty = no dictionary)
  char dictionaryName[32] = "";
  // Sleep wallpaper rendering quality. Pretty runs the OEM 3-pass grayscale pipeline;
  // Fast draws one 1-bit pass, which is two panel refreshes cheaper on every sleep.
  static constexpr uint8_t SLEEP_QUALITY_FAST = 0;
  static constexpr uint8_t SLEEP_QUALITY_PRETTY = 1;
  uint8_t sleepImageQuality = SLEEP_QUALITY_PRETTY;
  // Skip the unlock screen on a wallpaper wake and go straight back into the book.
  //
  // The sleep screen itself is untouched: the wallpaper is drawn and shown exactly as
  // before. This only changes what happens on the way OUT. Normally the wake re-reads the
  // .pxc, re-dithers every pixel, composites the unlock banners and refreshes the panel —
  // measured at ~3.6s of a ~4.7s wake on an X3 — and then the reader paints over all of
  // it anyway. With this on, none of that runs: the wallpaper stays on the panel from the
  // sleep until the reader's own first paint replaces it.
  //
  // Off by default. The cost is that the wake shows no sign of progress: the wallpaper
  // simply sits there until the book appears.
  uint8_t wakeStraightToBook = 1;
  // Open one of the books in progress at boot instead of resuming the last-read one
  // (0 = resume, 1 = pick at random). Held Back and a prior reader crash both skip it,
  // so it can never wedge boot.
  uint8_t openRandomRecentOnBoot = 0;
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // File browser listing order (0 = alphabetical, 1 = random). Random shuffles only the
  // files: folders stay sorted at the top, or navigating a deep card becomes a lottery.
  // A fresh shuffle is drawn every time a folder is opened, which is the point — it is
  // for finding something to read, not a stable sort.
  uint8_t bookBrowserRandomOrder = 0;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on).
  // Default ON: a finished book leaves the home in-progress list. Paging back before exit restores it.
  uint8_t removeReadBooksFromRecents = 1;
  // Move epub to /read folder on SD card when finished (0 = disabled, 1 = enabled).
  // Default ON: a finished book is filed away under /read.
  uint8_t moveFinishedToReadFolder = 1;
  // Move epub to /recents folder on SD card when it is opened and left unfinished
  // (0 = disabled, 1 = enabled). Default ON. Together with the setting above this keeps
  // /recents holding what is in progress and /read holding what is done; a finished book
  // goes to /read instead, so a book is never filed into both.
  uint8_t moveOpenedToRecentsFolder = 1;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  uint8_t backShortToFileBrowser = 0;
  // What a short Back press does on the home menu (HOME_BACK_ACTION)
  uint8_t homeBackAction = HOME_BACK_RESUME;
  // How the author is written after each title in the home in-progress list (AUTHOR_DISPLAY).
  // Initials stay the default because a full name competes with the title for the same
  // wrapped lines, and the list is read title-first.
  uint8_t authorDisplay = AUTHOR_INITIALS;
  // Image rendering mode in EPUB reader
  // Reading statistics. The idle threshold is stored in 10-second units so the
  // full 30-second to 10-minute range fits in one persisted byte.
  uint8_t readingStatsEnabled = 1;
  uint8_t readingStatsIdleUnits = 30;
  static constexpr uint8_t MIN_READING_STATS_IDLE_UNITS = 3;
  static constexpr uint8_t MAX_READING_STATS_IDLE_UNITS = 60;
  uint16_t readingStatsIdleSeconds() const { return static_cast<uint16_t>(readingStatsIdleUnits) * 10u; }
  // Retired: no Settings row, so nothing persists or changes it. Kept as the source the
  // per-book ReaderPrefs snapshot copies, so that field starts at IMAGES_DISPLAY too.
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  // Hold-to-wake threshold. 200 ms is long enough to reject a pocket brush but short
  // enough that the device feels instant in the hand. HalGPIO::verifyPowerButtonWakeup
  // returns as soon as the held time crosses this, so the wake happens under the finger
  // with no release required.
  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 200;
  }
  // Pop-up membership. The mask layout has exactly one owner: these three.
  bool isPopupItem(const uint8_t function) const { return (popupItems >> function) & 1u; }
  uint8_t popupItemCount() const {
    uint8_t n = 0;
    for (const uint8_t fn : POPUP_ITEM_FUNCTIONS) {
      if (isPopupItem(fn)) n++;
    }
    return n;
  }
  // Refuses to tick past POPUP_ITEM_MAX and reports whether the mask actually moved,
  // so the tick screen can say "full" instead of silently doing nothing.
  bool setPopupItem(const uint8_t function, const bool on) {
    if (isPopupItem(function) == on) return false;
    if (on && popupItemCount() >= POPUP_ITEM_MAX) return false;
    if (on) {
      popupItems |= static_cast<uint16_t>(1u << function);
    } else {
      popupItems &= static_cast<uint16_t>(~(1u << function));
    }
    return true;
  }

  int getReaderFontId() const;
  // Font id for the TXT reader, resolved from the txt* fields above rather than the
  // EPUB reader selection, so changing one never moves the other.
  int getTxtReaderFontId() const;
  // Per-book override: resolve the reader font id from a ReaderPrefs snapshot
  // instead of the live global fields, so a custom book lays out through its own
  // settings without ever mutating the global singleton.
  int getReaderFontId(const ReaderPrefs& prefs) const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships — otherwise the settings UI would keep
  // offering a size nothing renders at. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved text-rendering configuration for the Epub layout engine. The
  // viewport is renderer/orientation-derived, so the caller supplies it —
  // passing it in keeps a spec from ever existing in a half-filled state.
  // Deliberately unlocked: every field it reads is a single byte, so a
  // concurrent settings write can at worst produce a snapshot mixing pre- and
  // post-change fields, self-correcting on the next refresh. Locking here would
  // put a mutex on the render path and stall it behind saveToFile()'s SD write.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;
  // Per-book override: build the spec from a ReaderPrefs snapshot. Every field the
  // section cache keys on comes from prefs, so a custom book's cache is validated
  // and rebuilt against its own settings by CrossPoint's own indexing.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight, const ReaderPrefs& prefs) const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;

  /**
   * The name this reader shows to another reader during Nearby Position Sync.
   *
   * Falls back to "Lector-XXXX", the last two bytes of the WiFi MAC, when no
   * name has been set, so two readers on a table are still told apart without
   * anyone having to name them first.
   */
  const char* getEffectiveDeviceName() const;

  // ── Per-book reader-settings edit overlay ──────────────────────────────────
  // Overlays a book's ReaderPrefs onto the live reader fields so the existing
  // TextSettingsActivity edits them in place; endReaderEditOverlay() captures the
  // result and restores the true global values. While an overlay is active,
  // saveToFile() persists the global backup, never the book's overlaid values.
  //
  // The optional sink is how a per-book edit survives a power-off from inside the
  // settings screen: every saveToFile() during the overlay hands the live overlaid
  // values to it, so the owner can write the book's sidecar there and then. Plain
  // function pointer + context, not std::function — no closure allocation.
  using ReaderEditSink = void (*)(void* ctx, const ReaderPrefs& live);
  void applyReaderPrefs(const ReaderPrefs& p);
  void beginReaderEditOverlay(const ReaderPrefs& startValues, ReaderEditSink sink = nullptr, void* sinkCtx = nullptr);
  ReaderPrefs endReaderEditOverlay();
  bool readerEditOverlayActive() const { return readerEditOverlayActive_; }

  // Shadows PersistableStore::saveToFile so an active reader-edit overlay can never
  // leak a book's per-book values into the global settings.json.
  bool saveToFile() const;

  // ── Per-book status bar ────────────────────────────────────────────────────
  // The EPUB reader owns a book with its own status bar: master switch, and where
  // every item sits (ReaderPrefs, the sb* block). While that book is open the book's
  // values are overlaid onto the live sb* fields, so the hundred-odd places that draw
  // or measure the bar keep reading the same fields and cannot drift from each other.
  //
  // Runtime only. The global values are backed up on the way in, restored on the way
  // out, and swapped back for the duration of any saveToFile() that lands mid-book, so
  // a book's layout can never leak into settings.json and silently become the user's
  // global setting. Same guarantee the reader-edit overlay gives, same mechanism.
  void setStatusBarOverride(const ReaderPrefs& prefs);
  void clearStatusBarOverride();
  bool statusBarOverrideActive() const { return sbOverrideActive_; }
  bool statusBarEnabled() const { return sbEnabled != 0; }

  // ── Progress bars while the status bar is hidden ───────────────────────────
  // The Book Bar / Chapter Bar edges are part of the status bar, so hiding the bar
  // used to hide them as well. sbOffBar keeps them alive on their own: the edges,
  // the percentages and the draw path stay exactly as they are with the bar showing,
  // only the visibility gate and the thickness come from a different field.
  //
  // Everything that draws or reserves space for a progress bar must ask
  // progressBarsVisible() + activeBarThickness(), never statusBarEnabled() +
  // sbBarThickness, or the reserved band and the drawn bar disagree and the bar
  // paints over the reading text.
  bool progressBarsVisible() const { return statusBarEnabled() || sbOffBar != SB_OFFBAR_OFF; }
  // Gap between a floating progress bar and the screen edge, in pixels. Applied
  // to the outer edge and to both ends. Twelve reads clearly as a lifted pill on
  // the panel; six was too close to the bezel to be seen as deliberate. Every site
  // that draws OR reserves space for a bar must add it, or the two disagree and
  // the bar paints over the reading text.
  static constexpr int SB_FLOATING_BAR_MARGIN_PX = 12;
  int floatingBarMarginPx() const { return sbFloatingBar ? SB_FLOATING_BAR_MARGIN_PX : 0; }
  uint8_t activeBarThickness() const {
    if (statusBarEnabled() || sbOffBar == SB_OFFBAR_OFF) return sbBarThickness;
    return static_cast<uint8_t>(sbOffBar - 1);  // Slim/Medium/Fat -> 0/1/2
  }

 private:
  // Shared resolvers so getReaderFontId()/getReaderLineCompression() and their
  // ReaderPrefs overloads compute font id / line compression from one code path.
  int resolveReaderFontId(uint8_t fontFamily, uint8_t fontSize, const char* sdFontFamilyName) const;
  // Line-height multiplier from a line-spacing percentage (100 = natural). Clamped
  // to [MIN..MAX]_LINE_SPACING_PERCENT. Restored granular model (old lector).
  static float resolveLineCompression(uint8_t lineSpacingPercent);

  bool readerEditOverlayActive_ = false;
  // The global status bar block, held while a book's own layout is overlaid on it.
  bool sbOverrideActive_ = false;
  StatusBarBlock sbGlobalBackup_{};
  // Reads the live sb* fields into a block, and writes one back over them.
  StatusBarBlock captureStatusBarBlock() const;
  void applyStatusBarBlock(const StatusBarBlock& b);
  ReaderPrefs readerEditBackup_;
  ReaderEditSink readerEditSink_ = nullptr;
  void* readerEditSinkCtx_ = nullptr;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
