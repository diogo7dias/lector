#pragma once
#include <ArduinoJson.h>
#include <Epub/ReaderRenderSpec.h>
#include <PersistableStore.h>

#include <cstdint>

#include "ReaderPrefs.h"
#include "util/BoundMenuActions.h"
#include "util/MarginLink.h"

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  friend class PersistableStore<CrossPointSettings>;

 public:
  // Stored numbers are CrossPoint's. Lector's own older numbering differed at 7, 8 and 9;
  // the file's "sleepFaceNumbering" stamp tells the two apart, see wake_face::sleepFaceFromFile.
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    COVER_CUSTOM = 4,
    BLANK = 5,
    QUICK_RESUME = 6,
    TRANSPARENT_CUSTOM = 7,
    COVER_TRANSPARENT = 8,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };
  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Action for a short Back press on the home menu, where Back has no navigation target.
  enum HOME_BACK_ACTION {
    HOME_BACK_NONE = 0,
    HOME_BACK_RESUME = 1,
    HOME_BACK_STATS = 2,  // Retired: migrate to HOME_BACK_RESUME; keep persisted numbering.
    HOME_BACK_SORTES = 3,
    HOME_BACK_ACTION_COUNT
  };
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
  // Numbered like CrossPoint's so a settings file reads the same in both. NEXT_NEXT and
  // PREV_PREV turn the same way from either side key, for reading one-handed.
  enum SIDE_BUTTON_LAYOUT {
    PREV_NEXT = 0,
    NEXT_PREV = 1,
    SIDE_BUTTONS_DISABLED = 2,
    NEXT_NEXT = 3,
    PREV_PREV = 4,
    SIDE_BUTTON_LAYOUT_COUNT
  };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName).
  // ChareInk is the sole built-in reading family; more fonts are added from the SD card.
  enum FONT_FAMILY { CHAREINK = 0, FONT_FAMILY_COUNT };
  static constexpr uint8_t LEGACY_OPENDYSLEXIC = 2;
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Reader font size is a point size, not an enum slot — see fontPointSize.
  // Legacy 1.4-and-earlier files stored a 0..3 SMALL/MEDIUM/LARGE/EXTRA_LARGE
  // slot; fromJson() folds that range up (see LEGACY_FONT_SIZE_MAX).
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = 3;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
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
    // Turns off the reader's own periodic clean, matching upstream. It is not a
    // promise of no flashes: DisplayRefreshPolicy still forces one on ink debt,
    // which is what makes the option safe to offer at all. A fork-only "Never"
    // that disabled everything was removed in 060905bae because residue built up
    // over a long reading run on the X3.
    REFRESH_NEVER = 5,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions
  // How touch drives the open page: a master switch (touchReaderControls, 0 or 1) and one
  // gesture per page direction, numbered like CrossPoint so a settings file moves between
  // the two. Inverted Tap is tap-only; either direction on it swaps the shared tap zones.
  enum PAGE_TURN_GESTURE {
    TAP_AND_SWIPE = 0,
    TAP_ONLY = 1,
    SWIPE_ONLY = 2,
    INVERTED_TAP = 3,
    PAGE_TURN_GESTURE_DISABLED = 4,
    PAGE_TURN_GESTURE_COUNT
  };
  // How the reader menu opens on a touch board. Swipe Up is only offered where the
  // Home key is capacitive, since there the bottom edge is otherwise unused.
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };
  enum SHOW_READER_MENU { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2, SHOW_READER_MENU_COUNT };

  // How long Power must be held to wake the device from deep sleep. Normal rejects a
  // pocket brush; Fast wakes on any press, which is what Short Power Button Click = Sleep
  // used to force on everyone who picked it (one setting, two behaviours).

  // The bindable actions, defined in util/BoundMenuActions.h and re-exported here so
  // every CrossPointSettings::LP_MENU_* call site keeps working. The list itself lives
  // apart from this header because its rules are pure and host-tested; see that file
  // for the ordering constraints, the retired values and what each action means.
  using LONG_PRESS_MENU_FUNCTION = bound_action::LONG_PRESS_MENU_FUNCTION;
  static constexpr auto LP_MENU_KOSYNC = bound_action::LP_MENU_KOSYNC;
  static constexpr auto LP_MENU_DISABLED = bound_action::LP_MENU_DISABLED;
  static constexpr auto LP_MENU_BOOKMARK = bound_action::LP_MENU_BOOKMARK;
  static constexpr auto LP_MENU_DICTIONARY = bound_action::LP_MENU_DICTIONARY;
  static constexpr auto LP_MENU_GRAB_QUOTE = bound_action::LP_MENU_GRAB_QUOTE;
  static constexpr auto LP_MENU_SELECT_CHAPTER = bound_action::LP_MENU_SELECT_CHAPTER;
  static constexpr auto LP_MENU_GO_TO_PERCENT = bound_action::LP_MENU_GO_TO_PERCENT;
  static constexpr auto LP_MENU_GO_TO_PARAGRAPH = bound_action::LP_MENU_GO_TO_PARAGRAPH;
  static constexpr auto LP_MENU_FOOTNOTES = bound_action::LP_MENU_FOOTNOTES;
  static constexpr auto LP_MENU_TEXT_SETTINGS = bound_action::LP_MENU_TEXT_SETTINGS;
  static constexpr auto LP_MENU_READER_SETTINGS = bound_action::LP_MENU_READER_SETTINGS;
  static constexpr auto LP_MENU_TOGGLE_STATUS_BAR = bound_action::LP_MENU_TOGGLE_STATUS_BAR;
  static constexpr auto LP_MENU_POPUP = bound_action::LP_MENU_POPUP;
  static constexpr auto LP_MENU_WALLPAPER_HOLD = bound_action::LP_MENU_WALLPAPER_HOLD;
  static constexpr auto LP_MENU_BOOKMARKS = bound_action::LP_MENU_BOOKMARKS;
  static constexpr auto LP_MENU_VIEW_QUOTES = bound_action::LP_MENU_VIEW_QUOTES;
  static constexpr auto LP_MENU_NEARBY_SEND_BOOK = bound_action::LP_MENU_NEARBY_SEND_BOOK;
  static constexpr auto LP_MENU_PAGE_PREV = bound_action::LP_MENU_PAGE_PREV;
  static constexpr auto LP_MENU_PAGE_NEXT = bound_action::LP_MENU_PAGE_NEXT;
  static constexpr auto LP_MENU_GO_HOME = bound_action::LP_MENU_GO_HOME;
  static constexpr auto LP_MENU_BACK = bound_action::LP_MENU_BACK;
  static constexpr auto LP_MENU_LIGHT_PANEL = bound_action::LP_MENU_LIGHT_PANEL;
  static constexpr auto LP_MENU_SLEEP = bound_action::LP_MENU_SLEEP;
  static constexpr auto LP_MENU_FORCE_REFRESH = bound_action::LP_MENU_FORCE_REFRESH;
  static constexpr auto LP_MENU_WALLPAPER_DELETE = bound_action::LP_MENU_WALLPAPER_DELETE;
  static constexpr auto LP_MENU_WALLPAPER_FAVORITE = bound_action::LP_MENU_WALLPAPER_FAVORITE;
  static constexpr auto LP_MENU_CONTINUE_READING = bound_action::LP_MENU_CONTINUE_READING;
  static constexpr auto LP_MENU_RANDOM_BOOK = bound_action::LP_MENU_RANDOM_BOOK;
  static constexpr auto LP_MENU_SEARCH = bound_action::LP_MENU_SEARCH;
  static constexpr auto LP_MENU_SETTINGS = bound_action::LP_MENU_SETTINGS;
  static constexpr auto LP_MENU_ROTATE = bound_action::LP_MENU_ROTATE;
  static constexpr auto LP_MENU_READING_STATS = bound_action::LP_MENU_READING_STATS;
  static constexpr auto LP_MENU_ALIGN_LEFT = bound_action::LP_MENU_ALIGN_LEFT;
  static constexpr auto LP_MENU_ALIGN_JUSTIFY = bound_action::LP_MENU_ALIGN_JUSTIFY;
  static constexpr auto LP_MENU_TOGGLE_PARAGRAPH_SPACING = bound_action::LP_MENU_TOGGLE_PARAGRAPH_SPACING;
  static constexpr auto LP_MENU_QUICK_LOCK = bound_action::LP_MENU_QUICK_LOCK;
  static constexpr auto LP_MENU_TOGGLE_FRONTLIGHT = bound_action::LP_MENU_TOGGLE_FRONTLIGHT;
  static constexpr auto LP_MENU_OPEN_READER_MENU = bound_action::LP_MENU_OPEN_READER_MENU;
  static constexpr auto LP_MENU_CONFIRM = bound_action::LP_MENU_CONFIRM;
  static constexpr auto LP_MENU_NEXT_CHAPTER = bound_action::LP_MENU_NEXT_CHAPTER;
  static constexpr auto LP_MENU_PREV_CHAPTER = bound_action::LP_MENU_PREV_CHAPTER;
  static constexpr auto LONG_PRESS_MENU_FUNCTION_COUNT = bound_action::LONG_PRESS_MENU_FUNCTION_COUNT;

  // The three buttons the Buttons screen binds, in the order it lists them. Left and
  // Right are the two side keys; Home is the capacitive key under the panel on the
  // boards that have one (BoardConfig::hasHomeKey).
  enum BOUND_BUTTON : uint8_t {
    BOUND_BTN_LEFT = 0,
    BOUND_BTN_RIGHT = 1,
    BOUND_BTN_HOME = 2,
    BOUND_BTN_POWER = 3,
    BOUND_BTN_COUNT = 4
  };
  // The three gestures each of those carries.
  enum BOUND_GESTURE : uint8_t {
    BOUND_SINGLE = 0,
    BOUND_DOUBLE = 1,
    BOUND_HOLD = 2,
    BOUND_GESTURE_COUNT = 3,
  };

  // Actions that may be ticked into the pop-up: every binding value except Disabled
  // and Menu Pop-up itself. Used by the tick screen and by the pop-up builder, so
  // both agree on the row order without either owning a second list.
  static constexpr uint8_t POPUP_ITEM_FUNCTIONS[] = {
      LP_MENU_KOSYNC,          LP_MENU_BOOKMARK,          LP_MENU_BOOKMARKS,       LP_MENU_DICTIONARY,
      LP_MENU_GRAB_QUOTE,      LP_MENU_VIEW_QUOTES,       LP_MENU_GO_TO_PARAGRAPH, LP_MENU_FOOTNOTES,
      LP_MENU_READER_SETTINGS, LP_MENU_TOGGLE_STATUS_BAR, LP_MENU_WALLPAPER_HOLD,  LP_MENU_WALLPAPER_FAVORITE,
      LP_MENU_WALLPAPER_DELETE};

  // Cap on ticked pop-up rows. Every action can be ticked at once; the ceiling only exists
  // because popupItems is a bit mask, so its width is as many bits as there are to set. The
  // pop-up itself no longer needs the cap to stay on screen — option_popup::compute()
  // tightens its spacing when the rows would otherwise run off the panel.
  static constexpr uint8_t POPUP_ITEM_MAX = 32;

  // Image rendering in EPUB reader. Display draws images with grey levels, Placeholder
  // leaves the alt text only, Suppress hides them, and Black & White dithers them to two
  // levels so an image page skips the grey refresh. Black & White lays out like Display
  // (makeRenderSpec folds it), so switching between them keeps the section cache.
  enum IMAGE_RENDERING {
    IMAGES_DISPLAY = 0,
    IMAGES_PLACEHOLDER = 1,
    IMAGES_SUPPRESS = 2,
    IMAGES_DISPLAY_BW = 3,
    IMAGE_RENDERING_COUNT
  };

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

  // How large those paragraph numbers are drawn: Cozette digits at 1x, 2x or 3x their
  // 13px cell (see paragraphNumberScale). The stored values are the old 35%, 65% and
  // 100%, which those scales roughly match. Per-book, like the mode above.
  enum PARAGRAPH_NUMBER_SIZE {
    PARA_NUM_SIZE_SMALL = 0,
    PARA_NUM_SIZE_MEDIUM = 1,
    PARA_NUM_SIZE_LARGE = 2,
    PARAGRAPH_NUMBER_SIZE_COUNT
  };

  // Where those numbers sit across: a column at the page's left edge, or tucked into
  // the indent just before the paragraph's first letter. Per-book, like the size.
  enum PARAGRAPH_NUMBER_PLACE { PARA_NUM_PLACE_EDGE = 0, PARA_NUM_PLACE_TEXT = 1, PARAGRAPH_NUMBER_PLACE_COUNT };

  // Sleep screen settings
  uint8_t sleepScreen = CUSTOM;
  // Use Quick Resume on idle-timeout sleep while keeping sleepScreen for manual locks.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;
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
  // Status bar defaults. Chapter title top-left on its own line, the page in that chapter
  // bottom-left, the two chapter readouts bottom-right, the two session counters
  // bottom-centre, and a chapter progress bar along the bottom edge. Battery, clock, book
  // percentage and the book bar are off: the chapter is what a page is read against.
  uint8_t sbEnabled = 1;
  uint8_t sbBatteryPos = SB_ANCHOR_OFF;      // battery anchor
  uint8_t sbClockPos = SB_ANCHOR_OFF;        // clock anchor (X3 RTC only)
  uint8_t sbTitlePos = SB_ANCHOR_TL;         // chapter title anchor
  uint8_t sbTitleSource = SB_TITLE_CHAPTER;  // retired, no longer saved: see StatusBarBlock::foldTitleSource
  uint8_t sbTitleTruncate = 0;               // 0 = greedy, no ellipsis (drives reflow); 1 = clip with ellipsis
  uint8_t sbPagePos = SB_ANCHOR_BL;          // page-in-chapter anchor
  uint8_t sbPageFormat = SB_PAGE_FRACTION;   // "3/40" vs "8 left"
  uint8_t sbBookPctPos = SB_ANCHOR_OFF;      // book % (B:NN%) anchor
  uint8_t sbChapterPctPos = SB_ANCHOR_BR;    // chapter % (C:NN%) anchor
  uint8_t sbChapterNumPos = SB_ANCHOR_BR;    // chapter #/total (Ch N/M) anchor
  uint8_t sbSessionPagesPos = SB_ANCHOR_BC;  // pages turned this sitting (+N) anchor
  uint8_t sbParaPagesPos = SB_ANCHOR_BC;     // pages left in this paragraph (>P.N) anchor
  uint8_t sbBookTitlePos = SB_ANCHOR_OFF;    // book title anchor; on the chapter title's, stacked above it
  uint8_t sbBookBar = SB_EDGE_OFF;           // book progress bar edge (Off/Top/Bottom)
  uint8_t sbChapterBar = SB_EDGE_BOTTOM;     // chapter progress bar edge
  uint8_t sbBarThickness = SB_BAR_MEDIUM;    // progress bar thickness slim/med/fat
  // Retired options (Floating Bar, Bar Outline), no longer read or saved. Kept only
  // because the per-book field lists copy them into ReaderPrefs.
  uint8_t sbFloatingBar = 0;
  uint8_t sbBarOutline = 0;
  // Off / Slim / Medium / Fat. Only consulted while the status bar is hidden, where it
  // keeps the configured Book Bar / Chapter Bar edges drawing at its own thickness.
  // Off (the default) is the old behaviour: hiding the bar hides its progress bars too.
  uint8_t sbOffBar = SB_OFFBAR_OFF;
  // Clock time zone: an index into clock_zones::TABLE (lib/hal/ClockZones.h, CrossPoint's table
  // in CrossPoint's order, append only). 17 is UTC, clock_zones::UTC_INDEX: SettingsList.h checks
  // it. A file with no valid zone (CrossPoint writes 255 for "never chosen") takes one from the UTC
  // offset and city 0.58.0 stored (see migrateFromJson).
  uint8_t clockTimezone = 17;
  // Summer time: 0 follows the zone's own rule, 1 forces it on, 2 forces it off.
  uint8_t clockDst = 0;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  static constexpr uint8_t MIN_WORD_SPACING = reader_defaults::MIN_WORD_SPACING;
  static constexpr uint8_t MAX_WORD_SPACING = reader_defaults::MAX_WORD_SPACING;
  uint8_t wordSpacing = reader_defaults::WORD_SPACING;  // percent of natural space advance; no limit on justification
  // On by default, as in CrossPoint (Diogo, 2026-11-18). The grey pass costs an extra
  // refresh on every page turn; the toggle turns it off.
  uint8_t textAntiAliasing = 1;
  // How dark anti-aliased greys are drawn (0 Normal, 1 High, 2 Max); only with textAntiAliasing.
  uint8_t textContrast = 0;
  // Whole pixels between letters, offset: 2 is the font as designed, 0..4 shows as -2..+2.
  uint8_t characterSpacing = reader_defaults::CHARACTER_SPACING;
  // On, with both directions on Swipe Only by default: a swipe works the same wherever the
  // thumb lands, and leaves the whole page free of invisible tap targets.
  uint8_t touchReaderControls = 1;
  uint8_t pageTurnGesture = SWIPE_ONLY;
  uint8_t previousPageGesture = SWIPE_ONLY;
  // Persisted under the legacy "tapForReaderMenu" key, whose old values line up:
  // 0 = Off, 1 = Tap.
  uint8_t showReaderMenu = READER_MENU_TAP;
  // Wake hold. Defaults to Normal, including for a settings file that predates this
  // setting and chose Sleep — fromJson carries those forward to Fast, so nobody's wake
  // gets slower without them asking (see the migration there).
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonFollowOrientation = 0;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = CHAREINK;
  // Point size of the reader font (upstream #2720 replaced the SMALL/MEDIUM/LARGE
  // enum with a real point size). Only sizes the active family actually ships are
  // selectable; SdCardFontSystem::ensureLoaded() snaps this to the nearest
  // available size (and persists the snap) whenever the family changes.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  // Reader line spacing as a percentage of the font's natural line height (100 =
  // natural). Restored granular control (old lector). The resolved line-compression
  // float is part of the cache key, so a change rebuilds the section cache.
  static constexpr uint8_t MIN_LINE_SPACING_PERCENT = reader_defaults::MIN_LINE_SPACING_PERCENT;
  static constexpr uint8_t MAX_LINE_SPACING_PERCENT = reader_defaults::MAX_LINE_SPACING_PERCENT;
  uint8_t lineSpacingPercent = 95;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting. Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // Touch Sleep (touch boards): after this long without input the touch controller
  // sleeps and the chip waits on the buttons alone. The next button press wakes
  // touch; until then a touch does nothing, the capacitive Home key included (the
  // GT911 senses it). 0 Off, 1 15 s, 2 30 s, 3 1 min. Off by default: dead touch
  // mid-book read as a bug.
  uint8_t touchSleep = 0;
  // Wake from deep sleep when a USB cable is plugged in (0 = off, 1 = on). X4 Pro only.
  uint8_t wakeOnUsbPlugIn = 0;
  unsigned long touchSleepMs() const {
    static constexpr unsigned long MS[] = {0, 15000, 30000, 60000};
    return touchSleep < 4 ? MS[touchSleep] : 0;
  }
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  // Full refresh on the first page of each chapter entered, on top of the page count.
  uint8_t chapterStartRefresh = 0;
  // A crosshatch frame before the first page of each chapter entered (one extra refresh).
  uint8_t chapterWipe = 0;
  // What a short press of Confirm (or the Reader Menu binding) opens in a book: the tabbed
  // list, or a toolbar sheet over the page (scrub the book, then Contents / Text / More).
  enum READER_MENU_STYLE { READER_MENU_LIST = 0, READER_MENU_TOOLBAR = 1, READER_MENU_STYLE_COUNT };
  uint8_t readerMenuStyle = READER_MENU_LIST;

  // Reader screen margins. screenMargin is the horizontal (left/right) margin, shared by
  // both sides. The vertical margins always live in screenMarginTop/Bottom;
  // marginLinkMode only says how editing one writes the others, so every reader can read
  // the three fields directly without asking which mode is on. Restored granular range
  // (old lector). Margins feed the viewport, so a change rebuilds the section cache
  // through the viewport dimensions (no cache-format bump needed).
  // The side margin runs 5 to 40 in steps of 5; top and bottom keep the wider range.
  static constexpr uint8_t SCREEN_MARGIN_MIN = 0;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 100;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 1;
  static constexpr uint8_t HORIZONTAL_MARGIN_MIN = reader_defaults::MIN_SCREEN_MARGIN;
  static constexpr uint8_t HORIZONTAL_MARGIN_MAX = reader_defaults::MAX_SCREEN_MARGIN;
  static constexpr uint8_t HORIZONTAL_MARGIN_STEP = reader_defaults::SCREEN_MARGIN_STEP;
  uint8_t screenMargin = reader_defaults::SCREEN_MARGIN;
  uint8_t screenMarginTop = reader_defaults::SCREEN_MARGIN;
  uint8_t screenMarginBottom = reader_defaults::SCREEN_MARGIN;
  // margin_link::Mode as a number: 0 = Separate, 1 = top and bottom move together,
  // 2 = All Sides (every side holds the horizontal value). See util/MarginLink.h.
  static constexpr uint8_t MARGIN_LINK_MODE_COUNT = margin_link::MODE_COUNT;
  uint8_t marginLinkMode = margin_link::toStored(margin_link::DEFAULT_MODE);

  // The margin fields as the one value the link rule operates on, and back again. Both
  // editors (Reader Settings and the global settings list) go through these, so neither
  // can write a mode without the mode's own consequences — see util/MarginLink.h.
  margin_link::State marginState() const {
    return {{screenMargin, screenMarginTop, screenMarginBottom}, dynamicMargins, margin_link::toMode(marginLinkMode)};
  }
  void applyMarginState(const margin_link::State& state) {
    screenMargin = state.margins.horizontal;
    screenMarginTop = state.margins.top;
    screenMarginBottom = state.margins.bottom;
    dynamicMargins = state.dynamicMargins;
    marginLinkMode = margin_link::toStored(state.mode);
  }
  void setMarginLinkMode(const margin_link::Mode mode) {
    applyMarginState(margin_link::applyMode(marginState(), mode));
  }
  // Re-assert the current mode over the margin fields. Called on every load and every
  // save, so an editor that writes the raw fields (the web settings API) cannot leave a
  // page whose sides disagree with the mode it says it is in.
  void normalizeMargins() { setMarginLinkMode(margin_link::toMode(marginLinkMode)); }
  // Auto-widen horizontal margins toward ~62 chars/line (0 = off, 1 = auto min 10px,
  // 2 = auto min 20px). Overrides the fixed horizontal margin when on. Feeds the
  // viewport width, so a change re-paginates.
  static constexpr uint8_t DYNAMIC_MARGINS_COUNT = 3;
  uint8_t dynamicMargins = 0;

  // First-line paragraph indent, counted in spaces of the reading font (0 to 5, 0 = flush).
  // Feeds the render spec, so a change rebuilds the section cache like any layout setting.
  uint8_t paragraphIndentSpaces = reader_defaults::PARAGRAPH_INDENT_SPACES;
  // OPDS download destination folder ("" = SD root). Global; edited from the
  // OPDS server list. Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen.
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Persisted via a category-less SettingInfo::Enum,
  // edited from the OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Read EPUB title/author metadata when building the Library index; off lists filenames only.
  uint8_t libraryUseMetadata = 1;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Defaults to Disabled so shortcut-based bookmark toggling remains opt-in.
  uint8_t longPressMenuFunction = LP_MENU_DISABLED;
  // Hold-Confirm function INSIDE the in-book menu. Shares the LONG_PRESS_MENU_FUNCTION
  // values so both bindings offer the same list. Held Confirm closes the menu and hands
  // the choice back to the reader, which owns the page every one of those functions
  // needs. Costs nothing when Disabled, and nothing when set: the menu acts on release,
  // so a plain tap is unaffected either way.
  uint8_t menuHoldFunction = LP_MENU_DISABLED;

  // Per-button bindings: what each button does for each gesture, in a book and out of
  // one. Values are LONG_PRESS_MENU_FUNCTION, so the picker offers the same list every
  // other binding does.
  //
  // Defaults keep the device as it was: the side keys turn pages in a book and step
  // the selection outside one, Home goes home, and every double click and hold is
  // Disabled. That matters beyond taste — a bound double click delays the single by
  // the double-click window, and a bound hold takes the press away from the firmware's
  // own hold behaviour (page repeat, list paging). Leaving them Disabled is what keeps
  // both free until the user asks for them.
  // The X4 Pro, the one board with a frontlight, starts with a Home double press and a
  // Power double click that toggle it and a Home hold that opens the reader menu (CrossPoint's
  // defaults). Elsewhere they stay Disabled: a bound double click delays the single by the
  // double-click window, which a board with nothing to toggle would pay for no reason. Only a
  // field absent from a saved file takes these; a binding the file holds is kept as it is.
#if FREEINK_DEVICE_X4PRO
  static constexpr uint8_t DEFAULT_FRONTLIGHT_TOGGLE = LP_MENU_TOGGLE_FRONTLIGHT;
  static constexpr uint8_t DEFAULT_HOME_READER_MENU = LP_MENU_OPEN_READER_MENU;
#else
  static constexpr uint8_t DEFAULT_FRONTLIGHT_TOGGLE = LP_MENU_DISABLED;
  static constexpr uint8_t DEFAULT_HOME_READER_MENU = LP_MENU_DISABLED;
#endif
  uint8_t btnBookLeftSingle = LP_MENU_PAGE_PREV;
  uint8_t btnBookLeftDouble = LP_MENU_DISABLED;
  uint8_t btnBookLeftHold = LP_MENU_DISABLED;
  uint8_t btnBookRightSingle = LP_MENU_PAGE_NEXT;
  uint8_t btnBookRightDouble = LP_MENU_DISABLED;
  uint8_t btnBookRightHold = LP_MENU_DISABLED;
  uint8_t btnBookHomeSingle = LP_MENU_GO_HOME;
  uint8_t btnBookHomeDouble = DEFAULT_FRONTLIGHT_TOGGLE;
  uint8_t btnBookHomeHold = DEFAULT_HOME_READER_MENU;
  uint8_t btnUiLeftSingle = LP_MENU_PAGE_PREV;
  uint8_t btnUiLeftDouble = LP_MENU_DISABLED;
  uint8_t btnUiLeftHold = LP_MENU_DISABLED;
  uint8_t btnUiRightSingle = LP_MENU_PAGE_NEXT;
  uint8_t btnUiRightDouble = LP_MENU_DISABLED;
  uint8_t btnUiRightHold = LP_MENU_DISABLED;
  uint8_t btnUiHomeSingle = LP_MENU_GO_HOME;
  uint8_t btnUiHomeDouble = DEFAULT_FRONTLIGHT_TOGGLE;
  uint8_t btnUiHomeHold = LP_MENU_DISABLED;
  // Power. Its defaults are what the old shortPwrBtn / doubleClickPowerFunction pair
  // defaulted to: a click does nothing, a hold sleeps. The hold keeps its own threshold
  // (sleepHoldMs), so leaving it on Sleep leaves the sleep hold exactly as it was.
  uint8_t btnBookPowerSingle = LP_MENU_DISABLED;
  uint8_t btnBookPowerDouble = DEFAULT_FRONTLIGHT_TOGGLE;
  uint8_t btnBookPowerHold = LP_MENU_SLEEP;
  uint8_t btnUiPowerSingle = LP_MENU_DISABLED;
  uint8_t btnUiPowerDouble = DEFAULT_FRONTLIGHT_TOGGLE;
  uint8_t btnUiPowerHold = LP_MENU_SLEEP;

  // The binding field for one button and gesture, in or out of a book. Returns a
  // pointer so both the settings rows and the router read the same storage.
  uint8_t* buttonBinding(const bool inBook, const uint8_t button, const uint8_t gesture) {
    const BindingField field = bindingField(inBook, button, gesture);
    return field ? &(this->*field) : nullptr;
  }
  using BindingField = uint8_t CrossPointSettings::*;
  // nullptr for a button or gesture out of range.
  static BindingField bindingField(const bool inBook, const uint8_t button, const uint8_t gesture) {
    static const BindingField table[2][BOUND_BTN_COUNT][BOUND_GESTURE_COUNT] = {
        {{&CrossPointSettings::btnUiLeftSingle, &CrossPointSettings::btnUiLeftDouble,
          &CrossPointSettings::btnUiLeftHold},
         {&CrossPointSettings::btnUiRightSingle, &CrossPointSettings::btnUiRightDouble,
          &CrossPointSettings::btnUiRightHold},
         {&CrossPointSettings::btnUiHomeSingle, &CrossPointSettings::btnUiHomeDouble,
          &CrossPointSettings::btnUiHomeHold},
         {&CrossPointSettings::btnUiPowerSingle, &CrossPointSettings::btnUiPowerDouble,
          &CrossPointSettings::btnUiPowerHold}},
        {{&CrossPointSettings::btnBookLeftSingle, &CrossPointSettings::btnBookLeftDouble,
          &CrossPointSettings::btnBookLeftHold},
         {&CrossPointSettings::btnBookRightSingle, &CrossPointSettings::btnBookRightDouble,
          &CrossPointSettings::btnBookRightHold},
         {&CrossPointSettings::btnBookHomeSingle, &CrossPointSettings::btnBookHomeDouble,
          &CrossPointSettings::btnBookHomeHold},
         {&CrossPointSettings::btnBookPowerSingle, &CrossPointSettings::btnBookPowerDouble,
          &CrossPointSettings::btnBookPowerHold}}};
    if (button >= BOUND_BTN_COUNT || gesture >= BOUND_GESTURE_COUNT) return nullptr;
    return table[inBook ? 1 : 0][button][gesture];
  }
  // Every per-button binding field, both contexts, all buttons and gestures.
  template <typename Visit>
  static void forEachBindingField(Visit&& visit) {
    for (const bool inBook : {false, true}) {
      for (uint8_t button = 0; button < BOUND_BTN_COUNT; ++button) {
        for (uint8_t gesture = 0; gesture < BOUND_GESTURE_COUNT; ++gesture) {
          visit(bindingField(inBook, button, gesture));
        }
      }
    }
  }
  bool anyButtonBindingIs(const uint8_t function) const {
    bool found = false;
    forEachBindingField([&](const BindingField field) { found = found || this->*field == function; });
    return found;
  }
  // Which actions the Menu Pop-up lists, as a bitmask indexed by LONG_PRESS_MENU_FUNCTION
  // value (bit 2 = Toggle Bookmark, and so on). A mask rather than a list so the row order
  // is always POPUP_ITEM_FUNCTIONS order and can never drift from the tick screen.
  // At most POPUP_ITEM_MAX bits are ever set; the tick screen enforces it.
  uint32_t popupItems = 0;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // The book's own CSS, one switch: text styling (weight, slant, decoration, super/sub,
  // direction, display:none) and layout (alignment, indents, margins, padding, book-set
  // image sizes). Written under the "embeddedStyle" key (1 = enabled, 0 = disabled).
  // Paragraph Alignment "Book's Style" needs this on.
  uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // Guide Dots — draw a middle dot (U+00B7) between words as a reading aid. Restored
  // (old lector). Feeds the render spec (changes word width), so it rebuilds the cache.
  uint8_t guideDotsEnabled = 0;
  // Hidden Dots — sub-option of Guide Dots: keep the widened inter-word gap the dots
  // create, but draw no dot in it. Only meaningful while guideDotsEnabled is on, and
  // it feeds the render spec too, so toggling it rebuilds the cache.
  uint8_t guideDotsHidden = 0;
  // Diagnostic: outline the reader text viewport (0 = off, 1 = on). Drawn as an
  // overlay after the page renders, so it never affects layout or the cache.
  uint8_t debugBorders = 0;
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
  uint8_t paperbackLookStatus = 0;
  // Default paragraph numbering for books that have no per-book override yet. A book
  // already carrying its own reader_override.bin keeps whatever it was set to in the
  // in-book menu; this only seeds the next book opened fresh.
  uint8_t paragraphNumbering = PARA_NUM_CHAPTER;
  // Default size for those numbers.
  uint8_t paragraphNumberSize = PARA_NUM_SIZE_SMALL;
  // Default position for those numbers.
  uint8_t paragraphNumberPlace = PARA_NUM_PLACE_TEXT;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Dictionary folder name under /dictionaries (empty = no dictionary)
  // Sized to DictionaryRegistry::MAX_NAME_BYTES (static_assert in SettingsList.h): a UTF-8 folder name takes
  // up to 3 bytes per character, and a name stored cut short no longer names a folder.
  char dictionaryName[128] = "";
  // Fast Unlock (1 = on). Shortens the recovery-chord settle window the wake waits out
  // before routing, 500 ms to 100 ms on the X3/X4 button ladder (the X4 Pro is 20 ms
  // either way). Chosen from code inspection, so the row exists to switch back without
  // a reflash if a wake ever lands in the recovery picker by itself. The clearing blank
  // can overlap this window. See wake_face::inputSettleMs.
  uint8_t fastUnlock = 1;
  // What boot opens by itself, before the ordinary routing has its say. LAST_BOOK always
  // opens the last-read book, RANDOM picks one of the books in progress. There is no OFF:
  // an unlock lands on a book. Held Back and a prior reader crash still skip both modes,
  // so neither can wedge boot.
  // BOOT_BOOK_OFF is retired but its stored value 0 must keep its slot: renumbering would
  // turn an existing "Off" into "Random". loadFromFile migrates any stored 0 to LAST.
  enum BOOT_BOOK : uint8_t { BOOT_BOOK_OFF = 0, BOOT_BOOK_LAST = 1, BOOT_BOOK_RANDOM = 2, BOOT_BOOK_COUNT };
  uint8_t bootBookMode = BOOT_BOOK_LAST;
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // File browser listing order. Random shuffles only the files: folders stay sorted at the
  // top, or navigating a deep card becomes a lottery. A fresh shuffle is drawn every time a
  // folder is opened, which is the point — it is for finding something to read, not a
  // stable sort. Recently Added reads each file's own FAT timestamp; Last Read reads the
  // read counter the readers stamp into each book's cache, so books never opened sink to
  // the bottom in name order.
  enum BOOK_ORDER : uint8_t {
    BOOK_ORDER_ALPHABETICAL = 0,
    BOOK_ORDER_RANDOM = 1,
    BOOK_ORDER_RECENTLY_ADDED = 2,
    BOOK_ORDER_LAST_READ = 3,
    BOOK_ORDER_COUNT
  };
  uint8_t bookBrowserOrder = BOOK_ORDER_ALPHABETICAL;
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
  // Where the battery percentage is left off (HIDE_BATTERY_PERCENTAGE): Never shows it
  // everywhere, In Reader hides it in the reader's status bar only, Always also drops it
  // from the header every other screen carries. The battery icon stays in all three.
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Image rendering mode in EPUB reader (IMAGE_RENDERING)
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;

  // Frontlight (X4 Pro and other boards the SDK reports a frontlight for). The
  // rows are filtered out of Settings on a board without one, but the values
  // still persist so moving an SD card between devices keeps them.
  uint8_t frontlightBrightness = 60;
  // The top of the brightness range, wherever brightness is set (the light panel's
  // track runs 0 to this). 30 by default: past it the light is too bright to read by.
  uint8_t frontlightMaxBrightness = 30;
  // Brings a stored brightness within the maximum. Called on boot and whenever either
  // value changes, so every place that applies the light sees a capped value.
  void clampFrontlightBrightness() {
    if (frontlightBrightness > frontlightMaxBrightness) frontlightBrightness = frontlightMaxBrightness;
  }
  uint8_t frontlightWarmth = 50;  // 0 = cool .. 100 = warm; ignored without warm/cold LEDs
  uint8_t frontlightOn = 0;
  // Bring the light back by itself on wake. On by default, as in CrossPoint (Diogo,
  // 2026-11-18). Brightness and warmth are remembered either way. See FrontlightBootPolicy.h.
  uint8_t frontlightRestoreOnWake = 1;
  // Daily schedule, read at each wake: quarter hours since local midnight (see
  // FrontlightBootPolicy.h). Needs a clock, so it is only offered on a board with one.
  uint8_t frontlightScheduleEnabled = 0;
  uint8_t frontlightScheduleOn = 80;   // 20:00
  uint8_t frontlightScheduleOff = 28;  // 07:00

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  // Hold-to-sleep threshold. 200 ms is long enough to reject a pocket brush but short
  // enough that the device feels instant in the hand.
  //
  // Short-press-to-sleep drops it to 10 ms, so the press itself sleeps the device rather
  // than waiting out a hold. That cannot coexist with a power double click: the main loop
  // would sleep on the FIRST press, long before the detector could see a second release,
  // so the bound function would never run. A bound double click therefore wins, and
  // sleeping goes back to the normal hold.
  uint16_t getSleepHoldMs() const { return shortPressSleeps() ? 10 : 400; }
  // True when Footnotes is on any of the power button's in-book gestures. Governs the
  // "Power returns from footnote" row, which only describes that binding.
  bool powerOpensFootnotes() const {
    return btnBookPowerSingle == LP_MENU_FOOTNOTES || btnBookPowerDouble == LP_MENU_FOOTNOTES ||
           btnBookPowerHold == LP_MENU_FOOTNOTES;
  }

  // True when anything at all opens the reader pop-up: the three legacy bindings or any
  // of the per-button ones. Settings offers the Pop-up Items tick screen only then, so a
  // screen that exists to fill a pop-up is not shown to someone who has no pop-up.
  bool anyBindingOpensPopup() const {
    if (longPressMenuFunction == LP_MENU_POPUP || menuHoldFunction == LP_MENU_POPUP) {
      return true;
    }
    return anyButtonBindingIs(LP_MENU_POPUP);
  }
  // Whether a single power press sleeps the device. Every consumer must ask this rather
  // than reading a binding itself, so the answer is decided in one place: the wake-hold
  // verification, the sleep threshold, and the shared Confirm/Power boards that route a
  // short press to Power instead of Confirm.
  bool shortPressSleeps() const { return btnUiPowerSingle == LP_MENU_SLEEP || btnBookPowerSingle == LP_MENU_SLEEP; }

  // True when Sleep is bound to any gesture, on any button, in either context, or to the
  // reader's Confirm long-press or menu hold. False here means the device has no manual
  // route to sleep at all, and getSleepTimeoutMs() caps the auto-sleep timeout in response;
  // see util/SleepTimeoutGuard.h.
  bool anySleepBinding() const {
    return anyButtonBindingIs(LP_MENU_SLEEP) || longPressMenuFunction == LP_MENU_SLEEP ||
           menuHoldFunction == LP_MENU_SLEEP;
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
      popupItems |= static_cast<uint32_t>(1u << function);
    } else {
      popupItems &= static_cast<uint32_t>(~(1u << function));
    }
    return true;
  }

  int getReaderFontId() const;
  // Per-book override: resolve the reader font id from a ReaderPrefs snapshot
  // instead of the live global fields, so a custom book lays out through its own
  // settings without ever mutating the global singleton.
  int getReaderFontId(const ReaderPrefs& prefs) const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships — otherwise the settings UI would keep
  // offering a size nothing renders at. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved text-rendering configuration for the Epub layout engine, built from a
  // ReaderPrefs snapshot (makeRenderSpec) with the font id resolved here. The viewport
  // is renderer/orientation-derived, so the caller supplies it. Every field the section
  // cache keys on comes from prefs, so a custom book's cache is validated and rebuilt
  // against its own settings by CrossPoint's own indexing.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight, const ReaderPrefs& prefs) const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

  float getReaderLineCompression() const;
  float getReaderLineCompression(const ReaderPrefs& prefs) const {
    return readerLineCompression(prefs.lineSpacingPercent);
  }
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

  // Writes the Text Settings rows of a look onto the global reader fields. The in-book
  // toggles and the status bar block are per-book and are never pushed from here.
  void applyReaderPrefs(const ReaderPrefs& p);

  // Shadows PersistableStore::saveToFile to re-assert the margin link mode first.
  bool saveToFile() const;

  // The global status bar: for every screen that is not a book with its own bar.
  StatusBarBlock statusBar() const;
  void setStatusBar(const StatusBarBlock& b);

 private:
  // Runs after current fields are decoded; reports whether the legacy document needs a resave.
  bool migrateFromJson(JsonVariantConst doc);

  // Shared resolver so getReaderFontId() and its ReaderPrefs overload compute the font
  // id from one code path.
  int resolveReaderFontId(uint8_t fontFamily, uint8_t fontSize, const char* sdFontFamilyName) const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
