#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <iosfwd>

#include "activities/reader/ReaderPrefs.h"

class CrossPointSettings {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

 public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings&) = delete;
  CrossPointSettings& operator=(const CrossPointSettings&) = delete;

  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    BLANK = 4,
    COVER_CUSTOM = 5,
    QUICK_RESUME = 6,
    UNTIL_DEATH = 7,         // "Random Logo": random image from the full logo table, no indicator
    RANDOM_LOGO_CUSTOM = 8,  // slept from reader -> custom wallpaper rotation; slept from elsewhere -> random logo
    STATS_DASHBOARD = 9,
    STATS_DASHBOARD_PLUS = 10,
    FREEZE = 11,  // keep the last reader page on the panel, draw a thin frame
    QUOTES = 12,  // show a random saved highlight/quote as the sleep screen
    SLEEP_SCREEN_MODE_COUNT
  };
  // Frame colour drawn around the screen in the FREEZE sleep mode.
  enum SLEEP_FRAME_COLOR { SLEEP_FRAME_BLACK = 0, SLEEP_FRAME_WHITE = 1, SLEEP_FRAME_COLOR_COUNT };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };

  // Which file type the sleep wallpaper playlist shows. The /sleep folder can
  // hold both, but the rotation is filtered to one so switching is instant.
  enum WALLPAPER_FORMAT { WALLPAPER_BMP = 0, WALLPAPER_PXC = 1, WALLPAPER_FORMAT_COUNT };

  // Sleep wallpaper render quality. FAST = single 1-bit dithered refresh
  // (~0.5 s panel time); PRETTY = OEM multi-pass grayscale (~4-6 s on X3).
  enum SLEEP_IMAGE_QUALITY { SLEEP_IMG_FAST = 0, SLEEP_IMG_PRETTY = 1, SLEEP_IMAGE_QUALITY_COUNT };

  // Status bar enum - legacy
  // Legacy status-bar enums (STATUS_BAR_MODE / _PROGRESS_BAR / _THICKNESS / _TITLE /
  // _CLOCK_MODE) were removed with the v1 fixed-slot renderer. XTC keeps its own mode.
  enum XTC_STATUS_BAR_MODE {
    XTC_STATUS_BAR_HIDE = 0,
    XTC_STATUS_BAR_BOTTOM = 1,
    XTC_STATUS_BAR_TOP = 2,
    XTC_STATUS_BAR_MODE_COUNT
  };

  // --- Per-item status bar model (v2). Each text item is parked at one of six
  // anchors (or Off). This is the only status-bar model; the legacy fixed-slot
  // fields and their v1 renderer were removed. ---
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

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName)
  enum FONT_FAMILY { BOOKERLY = 0, FONT_FAMILY_COUNT };
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Font size options (point sizes 12-16). Size 11 was dropped to reclaim flash;
  // the enum was reindexed (SIZE_12 is now 0). JsonSettingsIO migrates old
  // persisted indices once (see the fontSizeSchemaV2 remap in loadSettings).
  enum FONT_SIZE { SIZE_12 = 0, SIZE_13 = 1, SIZE_14 = 2, SIZE_15 = 3, SIZE_16 = 4, FONT_SIZE_COUNT };
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
    REFRESH_NEVER = 5,  // never auto full-refresh (getRefreshFrequency returns INT_MAX)
    REFRESH_FREQUENCY_COUNT
  };

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
    LP_MENU_GRAB_QUOTE = 3,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long press behavior
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,
    ORIENTATION_CHANGE = 2,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  // UI Theme. Lector is the only theme (the former "Classic"/BaseTheme look, renamed).
  // Value 0 is preserved so existing configs (all were Classic=0) map straight to it;
  // any stale higher value from a removed theme clamps back to LECTOR.
  enum UI_THEME { LECTOR = 0 };

  // Home screen layout: LIST = scrolling recent-books list (Lector home),
  // SINGLE_COVER = one big cover of the current book (upstream CrossPoint home).
  enum HOME_LAYOUT { HOME_LAYOUT_LIST = 0, HOME_LAYOUT_SINGLE_COVER = 1, HOME_LAYOUT_COUNT };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_NVERTED = 2, TILT_PAGE_TURN_COUNT };

  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Frame colour for the FREEZE sleep mode (0 = black, 1 = white)
  uint8_t sleepFrameColor = SLEEP_FRAME_BLACK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Sleep wallpaper file type shown by the rotation (see WALLPAPER_FORMAT).
  // Defaults to PXC (pre-dithered 2bpp, the recommended format).
  uint8_t wallpaperFormat = WALLPAPER_PXC;
  // Sleep wallpaper render quality (see SLEEP_IMAGE_QUALITY). Default PRETTY
  // (owner call after device comparison: the grayscale look wins day-to-day,
  // and the lock delay is pocketable since sleep is committed — no page-turn
  // risk). FAST drops the X3 lock to ~4.3 s measured (one 1-bit refresh,
  // near the panel floor for a clean full-image swap). PXC only — BMP
  // wallpapers always render through the grayscale path.
  uint8_t sleepImageQuality = SLEEP_IMG_PRETTY;
  // Overlay the wallpaper's filename on the sleep screen (bottom-left box). When
  // off, showSleepFavoriteBadge instead draws a small "F" badge for favorites.
  uint8_t showSleepImageFilename = 0;
  // Draw a one-shot timing line over the first reader page after a wake
  // (unlock-to-usable breakdown) — a debugging aid, off by default.
  uint8_t wakeDiagnostics = 0;
  uint8_t showSleepFavoriteBadge = 0;
  // Status bar: the legacy fixed-slot fields (statusBar / statusBar*) were removed
  // in favour of the per-item v2 model below. XTC keeps its own top/bottom overlay.
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // --- New per-item status bar model (v2). Migrated from the legacy fields above
  // via applyStatusBarV2Migration(); defaults below approximate the legacy look. ---
  uint8_t sbEnabled = 1;                     // master on/off
  uint8_t sbBatteryPos = SB_ANCHOR_BL;       // battery anchor
  uint8_t sbClockPos = SB_ANCHOR_OFF;        // clock anchor (X3 RTC only)
  uint8_t sbTitlePos = SB_ANCHOR_BC;         // title anchor
  uint8_t sbTitleSource = SB_TITLE_CHAPTER;  // book or chapter title
  uint8_t sbTitleTruncate = 0;               // 0 = greedy, no … (drives reflow); 1 = clip with …
  uint8_t sbPagePos = SB_ANCHOR_BR;          // page-in-chapter anchor
  uint8_t sbPageFormat = SB_PAGE_FRACTION;   // "3/40" vs "8 left"
  uint8_t sbBookPctPos = SB_ANCHOR_BR;       // book % (B:NN%) anchor
  uint8_t sbChapterPctPos = SB_ANCHOR_OFF;   // chapter % (C:NN%) anchor
  uint8_t sbChapterNumPos = SB_ANCHOR_OFF;   // chapter #/total (Ch N/M) anchor
  uint8_t sbBookBar = SB_EDGE_OFF;           // book progress bar edge
  uint8_t sbChapterBar = SB_EDGE_OFF;        // chapter progress bar edge
  uint8_t sbBarThickness = SB_BAR_MEDIUM;    // progress bar thickness slim/med/fat
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
  uint8_t textAntiAliasing = 0;  // permanently off; UI toggle removed, render path hardcoded false
  // Paperback Look: smear drawn glyph pixels +1px right/+1px down for heavier
  // ink. Two independent toggles, both default ON: body = reader page content
  // (EPUB + TXT), status = the reading-screen status bar. Menus/other UI unaffected.
  uint8_t paperbackLookBody = 1;
  uint8_t paperbackLookStatus = 1;
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
  uint8_t fontFamily = BOOKERLY;
  uint8_t fontSize = SIZE_14;
  // Legacy line-spacing enum (TIGHT/NORMAL/WIDE). Superseded by lineSpacingPercent;
  // retained only so the positional binary-settings reader stays aligned.
  uint8_t lineSpacing = NORMAL;
  // Reader line spacing as a percentage of the font's natural line height.
  // 100 = natural spacing; adjustable MIN..MAX_LINE_SPACING_PERCENT. Old configs
  // without this key default to 100 (== the old NORMAL), so existing readers are
  // unchanged; users who had picked Tight/Wide re-pick.
  uint8_t lineSpacingPercent = 100;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;
  // Diagnostic: outline the reader text viewport (0 = off, 1 = on). Drawn as an
  // overlay after the page renders, so it never affects layout or the cache.
  uint8_t debugBorders = 0;

  // Reader screen margin settings. screenMargin is the horizontal (left/right)
  // margin and also drives all four sides when uniformMargins is on. When
  // uniformMargins is off, screenMarginTop / screenMarginBottom take over the
  // vertical margins independently.
  uint8_t screenMargin = 5;
  uint8_t screenMarginTop = 5;
  uint8_t screenMarginBottom = 5;
  uint8_t uniformMargins = 1;  // 1 = all sides use screenMargin; 0 = separate H / Top / Bottom
  // Auto-widen horizontal margins toward ~62 chars/line (0 = off, 1 = auto min 10px,
  // 2 = auto min 20px). Overrides the fixed horizontal margin when on. Per-book via
  // ReaderPrefs; changing it re-paginates (viewport width changes).
  uint8_t dynamicMargins = 0;
  static constexpr uint8_t MIN_SCREEN_MARGIN = 0;
  static constexpr uint8_t MAX_SCREEN_MARGIN = 100;

  // First-line paragraph indent. BOOK = use the publisher/CSS indent (default);
  // PERCENT = custom, where 0% is flush with the other lines and 100% starts the
  // first line at the horizontal middle of the text column.
  enum FIRST_LINE_INDENT_MODE : uint8_t { FIRST_LINE_INDENT_BOOK = 0, FIRST_LINE_INDENT_PERCENT = 1 };
  uint8_t firstLineIndentMode = FIRST_LINE_INDENT_BOOK;
  uint8_t firstLineIndentPercent = 0;
  static constexpr uint8_t MAX_FIRST_LINE_INDENT_PERCENT = 100;
  // Reader spacing (EPUB layout). Changing either re-paginates the section cache
  // (bumped SECTION_FILE_VERSION). wordSpacing is stored as a 10%-step count so a
  // signed -30..+300% range fits a uint8_t (3 = 0%); paragraphSpacing is stored
  // directly as a 0..150 percentage of the line height (block gap; 0 = off).
  static constexpr uint8_t WORD_SPACING_ZERO = 3;        // stored step that means 0%
  static constexpr uint8_t MAX_WORD_SPACING = 33;        // +300%
  static constexpr uint8_t MAX_PARAGRAPH_SPACING = 150;  // 150%
  static constexpr int MIN_WORD_SPACING_PERCENT = -30;
  static constexpr int MAX_WORD_SPACING_PERCENT = 300;
  uint8_t wordSpacing = WORD_SPACING_ZERO;
  uint8_t paragraphSpacing = 0;
  // Signed word-spacing percentage (-30..+300) from the stored 10%-step value.
  int wordSpacingPercent() const { return (static_cast<int>(wordSpacing) - WORD_SPACING_ZERO) * 10; }
  // Convert a signed percentage back to the stored 10%-step value.
  static uint8_t wordSpacingStepFromPercent(int percent) {
    return static_cast<uint8_t>(percent / 10 + WORD_SPACING_ZERO);
  }
  // OPDS browser settings
  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Long-press page turn button behavior
  uint8_t longPressButtonBehavior = OFF;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Defaults to Disabled so shortcut-based bookmark toggling remains opt-in.
  uint8_t longPressMenuFunction = LP_MENU_DISABLED;
  // UI Theme
  uint8_t uiTheme = LECTOR;
  // Home screen layout (see HOME_LAYOUT). Default = recent-books list.
  uint8_t homeLayout = HOME_LAYOUT_LIST;
  // Open a random book from the Recent Books list on boot/wake instead of
  // resuming the last-read book (0 = off, 1 = on).
  uint8_t openRandomRecentOnBoot = 0;
  // File browser ordering for the books folder (0 = alphabetical, 1 = random).
  // Random reshuffles the book list on every folder load to help pick the next
  // read; directories stay grouped and sorted at the top.
  uint8_t bookBrowserRandomOrder = 0;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // Guide Dots - draws a middle dot (U+00B7) between words as a reading aid
  uint8_t guideDotsEnabled = 0;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Custom text for the lock/wake screen footer banner (empty = default "READ UNTIL YOU DIE.")
  char customFooter[64] = "";
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Move epub to /Read/ folder on SD card when finished (0 = disabled, 1 = enabled)
  uint8_t moveFinishedToReadFolder = 0;
  // Move a book file into the flat /recents/ folder the first time it is opened
  // from elsewhere (0 = off, 1 = on). Reading progress (hash-keyed cache dir) and
  // the _QUOTES.txt sidecar move with it, so nothing is lost. See BookRelocation.h.
  uint8_t moveOpenedToRecents = 0;
  // OPDS download destination folder ("" = SD root). Global; edited from the OPDS
  // server list. Persisted via a category-less SettingInfo::String in SettingsList.h,
  // so it stays out of the on-device Settings screen (upstream #2571).
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Category-less SettingInfo::Enum, cycled from the
  // OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Reading statistics. Idle threshold is stored in 10-second units so the
  // full 30-second to 10-minute range fits in one persisted byte.
  uint8_t readingStatsEnabled = 1;
  uint8_t readingStatsIdleUnits = 30;
  static constexpr uint8_t MIN_READING_STATS_IDLE_UNITS = 3;
  static constexpr uint8_t MAX_READING_STATS_IDLE_UNITS = 60;
  uint16_t readingStatsIdleSeconds() const { return static_cast<uint16_t>(readingStatsIdleUnits) * 10u; }
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Tilt-based page turning (X3 only — requires QMI8658 IMU)
  uint8_t tiltPageTurn = TILT_OFF;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Reader line-spacing percentage bounds (100 == the font's natural spacing).
  static constexpr uint8_t MIN_LINE_SPACING_PERCENT = 35;
  static constexpr uint8_t MAX_LINE_SPACING_PERCENT = 150;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;
  // Resolve a reader font id from an explicit font size + SD font name, using the
  // shared SD-font resolver. Lets a per-book ReaderPrefs resolve its own font id
  // without duplicating the SD-resolver/built-in fallback logic. See ReaderPrefs.
  int resolveReaderFontId(uint8_t size, const char* sdName) const;

  // ── Per-book reader-settings edit overlay ──────────────────────────────────
  // Copy the reader-tab layout fields from a ReaderPrefs into the live settings.
  void applyReaderPrefs(const ReaderPrefs& p);
  // While a book's in-book "Reader" tab is open, overlay its ReaderPrefs onto the
  // live reader fields so the existing SettingsActivity (Reader category) edits
  // them in place. saveToFile() persists the GLOBAL values (the backup taken here)
  // throughout, so settings.json never captures a book's per-book values even if
  // the caller — or a background task — saves mid-edit. See EpubReaderActivity.
  void beginReaderEditOverlay(const ReaderPrefs& startValues);
  // Capture the edited reader fields, restore the global backup, end the overlay.
  ReaderPrefs endReaderEditOverlay();
  bool readerEditOverlayActive() const { return readerEditOverlayActive_; }

  // If count_only is true, returns the number of settings items that would be written.
  uint8_t writeSettings(HalFile& file, bool count_only = false) const;

  bool saveToFile() const;
  bool loadFromFile();

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

 private:
  bool loadFromBinaryFile();
  bool migrateLanguageBinaryFile();

  // Per-book reader-settings edit overlay state (see beginReaderEditOverlay).
  bool readerEditOverlayActive_ = false;
  ReaderPrefs readerEditBackup_;

 public:
  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
