#pragma once

#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "activities/settings/SettingsActivity.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_BOOKERLY};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_BOOKERLY));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;

  // Getter/setter read the SD font registry LIVE via dynCtx (set below) instead of
  // capturing a copy of the family names, so they stay plain function pointers (no
  // heap closure). dynCtx = the registry passed to getSettingsList: nullptr (e.g.
  // JsonSettingsIO) makes them built-in-only, exactly as the empty captured list did.
  s.valueGetter = [](const void* ctx) -> uint8_t {
    const auto* reg = static_cast<const SdCardFontRegistry*>(ctx);
    // If an SD card font is selected, find its index in the live registry.
    if (reg && SETTINGS.sdFontFamilyName[0] != '\0') {
      const auto& families = reg->getFamilies();
      for (int i = 0; i < static_cast<int>(families.size()); i++) {
        if (families[i].name == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [](const void* ctx, uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
      return;
    }
    const auto* reg = static_cast<const SdCardFontRegistry*>(ctx);
    if (!reg) return;
    const auto& families = reg->getFamilies();
    const int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  };

  s.dynCtx = registry;
  return s;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// Built fresh on every call and owned by the caller. Deliberately NOT a
// function-local static: the cached list costs ~10KB of DRAM for the whole
// runtime, held even while reading — the tightest heap moment in the product.
// Rebuild cost is pure CPU (no I/O), paid only when Settings/web/JSON-IO
// actually need the list. When an SdCardFontRegistry is supplied AND has SD
// card fonts installed, the font-family entry is replaced with a
// registry-aware version.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr) {
  std::vector<SettingInfo> v;
  // Built with sequential push_back on purpose: a single braced initializer
  // materializes ALL ~73 SettingInfo elements (~10KB) as one backing array on
  // the caller's stack. That was safe when this list was built once at boot,
  // but per-call construction runs from deep call sites (settings toggles,
  // SETTINGS.saveToFile via JsonSettingsIO) where a 10KB spike overflows the
  // 16KB loop-task stack. push_back keeps one ~130B temporary at a time.
  v.reserve(84);
  // --- Display ---
  v.push_back(
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                         StrId::STR_COVER_CUSTOM, StrId::STR_QUICK_RESUME, StrId::STR_SLEEP_UNTIL_DEATH,
                         StrId::STR_SLEEP_RANDOM_LOGO_CUSTOM, StrId::STR_STATS_DASHBOARD,
                         StrId::STR_STATS_DASHBOARD_PLUS, StrId::STR_SLEEP_FREEZE, StrId::STR_SLEEP_QUOTES},
                        "sleepScreen", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_FRAME_COLOR, &CrossPointSettings::sleepFrameColor,
                                {StrId::STR_FRAME_BLACK, StrId::STR_FRAME_WHITE}, "sleepFrameColor",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_WALLPAPER_FORMAT, &CrossPointSettings::wallpaperFormat,
                                {StrId::STR_WALLPAPER_BMP, StrId::STR_WALLPAPER_PXC}, "wallpaperFormat",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_IMAGE_QUALITY, &CrossPointSettings::sleepImageQuality,
                                {StrId::STR_SLEEP_IMG_FAST, StrId::STR_SLEEP_IMG_PRETTY}, "sleepImageQuality",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_SLEEP_IMAGE_FILENAME, &CrossPointSettings::showSleepImageFilename,
                                  "showSleepImageFilename", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Toggle(StrId::STR_WAKE_DIAGNOSTICS, &CrossPointSettings::wakeDiagnostics, "wakeDiagnostics",
                                  StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_SLEEP_FAVORITE_BADGE, &CrossPointSettings::showSleepFavoriteBadge,
                                  "showSleepFavoriteBadge", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::String(StrId::STR_SLEEP_FOOTER_TEXT, SETTINGS.customFooter, sizeof(SETTINGS.customFooter),
                                  "customFooter", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                                {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                                {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                                {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                                 StrId::STR_PAGES_30, StrId::STR_NEVER},
                                "refreshFrequency", StrId::STR_CAT_DISPLAY));
  // Theme picker removed: Lector is the only theme (see UI_THEME). uiTheme stays
  // as a field (defaults to LECTOR) but is no longer user-selectable.
  v.push_back(SettingInfo::Enum(StrId::STR_HOME_LAYOUT, &CrossPointSettings::homeLayout,
                                {StrId::STR_HOME_LAYOUT_LIST, StrId::STR_HOME_LAYOUT_SINGLE_COVER}, "homeLayout",
                                StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Toggle(StrId::STR_OPEN_RANDOM_ON_BOOT, &CrossPointSettings::openRandomRecentOnBoot,
                                  "openRandomRecentOnBoot", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Enum(StrId::STR_BOOK_BROWSER_ORDER, &CrossPointSettings::bookBrowserRandomOrder,
                                {StrId::STR_BOOK_ORDER_ALPHABETICAL, StrId::STR_BOOK_ORDER_RANDOM},
                                "bookBrowserRandomOrder", StrId::STR_CAT_DISPLAY));
  v.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                  StrId::STR_CAT_DISPLAY));
  // --- Reader ---
  // Built-in font-family entry. Replaced per-call with a registry-aware
  // version when SD fonts are installed.
  v.push_back(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily, {StrId::STR_BOOKERLY},
                                "fontFamily", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                                {StrId::STR_FONT_SIZE_12, StrId::STR_FONT_SIZE_13, StrId::STR_FONT_SIZE_14,
                                 StrId::STR_FONT_SIZE_15, StrId::STR_FONT_SIZE_16},
                                "fontSize", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(
      StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacingPercent,
      {CrossPointSettings::MIN_LINE_SPACING_PERCENT, CrossPointSettings::MAX_LINE_SPACING_PERCENT, 5},
      "lineSpacingPercent", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_UNIFORM_MARGINS, &CrossPointSettings::uniformMargins, "uniformMargins",
                                  StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                                 {CrossPointSettings::MIN_SCREEN_MARGIN, CrossPointSettings::MAX_SCREEN_MARGIN, 1},
                                 "screenMargin", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN_TOP, &CrossPointSettings::screenMarginTop,
                                 {CrossPointSettings::MIN_SCREEN_MARGIN, CrossPointSettings::MAX_SCREEN_MARGIN, 1},
                                 "screenMarginTop", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN_BOTTOM, &CrossPointSettings::screenMarginBottom,
                                 {CrossPointSettings::MIN_SCREEN_MARGIN, CrossPointSettings::MAX_SCREEN_MARGIN, 1},
                                 "screenMarginBottom", StrId::STR_CAT_READER));
  v.push_back(
      SettingInfo::Enum(StrId::STR_DYNAMIC_MARGINS, &CrossPointSettings::dynamicMargins,
                        {StrId::STR_DYNAMIC_MARGINS_OFF, StrId::STR_DYNAMIC_MARGINS_10, StrId::STR_DYNAMIC_MARGINS_20},
                        "dynamicMargins", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Enum(StrId::STR_PARAGRAPH_NUMBERS, &CrossPointSettings::paragraphNumbering,
                                {StrId::STR_PARA_NUM_OFF, StrId::STR_PARA_NUM_CHAPTER, StrId::STR_PARA_NUM_BOOK},
                                "paragraphNumbering", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Enum(StrId::STR_FIRST_LINE_INDENT, &CrossPointSettings::firstLineIndentMode,
                                {StrId::STR_INDENT_BOOK, StrId::STR_INDENT_PERCENT}, "firstLineIndentMode",
                                StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(StrId::STR_FIRST_LINE_INDENT_PERCENT, &CrossPointSettings::firstLineIndentPercent,
                                 {0, CrossPointSettings::MAX_FIRST_LINE_INDENT_PERCENT, 5}, "firstLineIndentPercent",
                                 StrId::STR_CAT_READER));
  // Both open custom % pickers on device (intercepted by nameId); the ranges
  // here drive persistence + the web form. wordSpacing is a 10%-step count.
  v.push_back(SettingInfo::Value(StrId::STR_WORD_SPACING, &CrossPointSettings::wordSpacing,
                                 {0, CrossPointSettings::MAX_WORD_SPACING, 1}, "wordSpacing", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Value(StrId::STR_PARAGRAPH_SPACING, &CrossPointSettings::paragraphSpacing,
                                 {0, CrossPointSettings::MAX_PARAGRAPH_SPACING, 10}, "paragraphSpacing",
                                 StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Enum(
      StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
      {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE},
      "paragraphAlignment", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                                  StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                  "focusReadingEnabled", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_GUIDE_DOTS, &CrossPointSettings::guideDotsEnabled, "guideDotsEnabled",
                                  StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                                  StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Enum(
      StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
      {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
      "orientation", StrId::STR_CAT_READER));
  v.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                  "extraParagraphSpacing", StrId::STR_CAT_READER));
  // Text Anti-Aliasing removed from the UI on purpose — permanently off
  // (see needsTextGrayscale in EpubReaderActivity: the greyscale text pass
  // is imperceptible here but causes a fading grey refresh every page).
  v.push_back(SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                                {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                                "imageRendering", StrId::STR_CAT_READER));
  // --- Controls ---
  v.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                                StrId::STR_CAT_CONTROLS));
  v.push_back(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION,
                                  &CrossPointSettings::frontButtonFollowOrientation, "frontButtonFollowOrientation",
                                  StrId::STR_CAT_CONTROLS));
  v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                 StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                                "longPressButtonBehavior", StrId::STR_CAT_CONTROLS));
  v.push_back(
      SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                        {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION, StrId::STR_GRAB_QUOTE},
                        "longPressMenuFunction", StrId::STR_CAT_CONTROLS));
  v.push_back(SettingInfo::Enum(
      StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
      {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES},
      "shortPwrBtn", StrId::STR_CAT_CONTROLS));
  v.push_back(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                                  "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS));
  // --- System ---
  v.push_back(SettingInfo::Value(
      StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
      {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
      "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                                  StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                                  "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                                  "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_OPENED_TO_RECENTS, &CrossPointSettings::moveOpenedToRecents,
                                  "moveOpenedToRecents", StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Toggle(StrId::STR_DEBUG_BORDERS, &CrossPointSettings::debugBorders, "debugBorders",
                                  StrId::STR_CAT_READER));
  // OPDS download folder + filename format: persisted + web-exposed, but
  // category-less so they stay off the on-device Settings screen (edited via
  // the OPDS server list UI instead). Upstream #2571.
  v.push_back(SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                                  sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"));
  v.push_back(SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                                {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                                "opdsFilenameFormat"));
  v.push_back(SettingInfo::Toggle(StrId::STR_TRACK_READING_STATS, &CrossPointSettings::readingStatsEnabled,
                                  "readingStatsEnabled", StrId::STR_CAT_SYSTEM));
  v.push_back(SettingInfo::Value(
      StrId::STR_READING_IDLE_LIMIT, &CrossPointSettings::readingStatsIdleUnits,
      {CrossPointSettings::MIN_READING_STATS_IDLE_UNITS, CrossPointSettings::MAX_READING_STATS_IDLE_UNITS, 1},
      "readingStatsIdleUnits", StrId::STR_CAT_SYSTEM));
  // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
  v.push_back(SettingInfo::DynamicString(
      StrId::STR_KOREADER_USERNAME, [](const void*) { return KOREADER_STORE.getUsername(); },
      [](const void*, const std::string& v) {
        KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
        KOREADER_STORE.saveToFile();
      },
      "koUsername", StrId::STR_KOREADER_SYNC));
  v.push_back(SettingInfo::DynamicString(
      StrId::STR_KOREADER_PASSWORD, [](const void*) { return KOREADER_STORE.getPassword(); },
      [](const void*, const std::string& v) {
        KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
        KOREADER_STORE.saveToFile();
      },
      "koPassword", StrId::STR_KOREADER_SYNC));
  v.push_back(SettingInfo::DynamicString(
      StrId::STR_SYNC_SERVER_URL, [](const void*) { return KOREADER_STORE.getServerUrl(); },
      [](const void*, const std::string& v) {
        KOREADER_STORE.setServerUrl(v);
        KOREADER_STORE.saveToFile();
      },
      "koServerUrl", StrId::STR_KOREADER_SYNC));
  v.push_back(SettingInfo::DynamicEnum(
      StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
      [](const void*) { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
      [](const void*, uint8_t v) {
        KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
        KOREADER_STORE.saveToFile();
      },
      "koMatchMethod", StrId::STR_KOREADER_SYNC));
  v.push_back(SettingInfo::DynamicEnum(
      StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
      [](const void*) { return KOREADER_STORE.getSendMetadata() ? (uint8_t)1 : (uint8_t)0; },
      [](const void*, uint8_t v) {
        KOREADER_STORE.setSendMetadata(v != 0);
        KOREADER_STORE.saveToFile();
      },
      "koSendMetadata", StrId::STR_KOREADER_SYNC));
  v.push_back(SettingInfo::DynamicEnum(
      StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
      [](const void*) { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
      [](const void*, uint8_t v) {
        KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
        KOREADER_STORE.saveToFile();
      },
      "koSyncBehavior", StrId::STR_KOREADER_SYNC));
  // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
  // The legacy fixed-slot toggles (chapter/page count, book %, progress bar,
  // title, battery) were removed; placement is now the per-item v2 sb* model.
  v.push_back(SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                                {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  // Clock entries (web settings only; device UI uses ClockOffsetActivity for the offset).
  // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
  v.push_back(SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                                 "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                                {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
  // on next WiFi connect, which is useful when crossing time zones.
  v.push_back(SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced,
                                  "clockHasBeenSynced", StrId::STR_CUSTOMISE_STATUS_BAR));
  // --- New per-item status bar model (v2). Anchor enums share the same 7-value
  // list {Off, TL, TC, TR, BL, BC, BR}; the device UI (StatusBarSettingsActivity)
  // renders these as the [XX] position popup. ---
  v.push_back(SettingInfo::Toggle(StrId::STR_STATUS_BAR, &CrossPointSettings::sbEnabled, "sbEnabled",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_BATTERY, &CrossPointSettings::sbBatteryPos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbBatteryPos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::sbClockPos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbClockPos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::sbTitlePos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbTitlePos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_TITLE_SOURCE, &CrossPointSettings::sbTitleSource,
                                {StrId::STR_BOOK, StrId::STR_CHAPTER}, "sbTitleSource",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Toggle(StrId::STR_TRUNCATE_TITLE, &CrossPointSettings::sbTitleTruncate, "sbTitleTruncate",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_PAGE_IN_CHAPTER, &CrossPointSettings::sbPagePos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbPagePos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_PAGE_FORMAT, &CrossPointSettings::sbPageFormat,
                                {StrId::STR_PAGE_FRACTION, StrId::STR_PAGE_LEFT}, "sbPageFormat",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_BOOK_PERCENT, &CrossPointSettings::sbBookPctPos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbBookPctPos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_CHAPTER_PERCENT, &CrossPointSettings::sbChapterPctPos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbChapterPctPos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_CHAPTER_NUMBER, &CrossPointSettings::sbChapterNumPos,
                                {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                                 StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                                "sbChapterNumPos", StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_BOOK_BAR, &CrossPointSettings::sbBookBar,
                                {StrId::STR_STATE_OFF, StrId::STR_TOP, StrId::STR_BOTTOM}, "sbBookBar",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_CHAPTER_BAR, &CrossPointSettings::sbChapterBar,
                                {StrId::STR_STATE_OFF, StrId::STR_TOP, StrId::STR_BOTTOM}, "sbChapterBar",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  v.push_back(SettingInfo::Enum(StrId::STR_BAR_THICKNESS, &CrossPointSettings::sbBarThickness,
                                {StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_FAT}, "sbBarThickness",
                                StrId::STR_CUSTOMISE_STATUS_BAR));
  // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
  if (halTiltSensor.isAvailable()) {
    // Insert after the short power button setting (end of Controls section)
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (it->nameId == StrId::STR_SHORT_PWR_BTN) {
        v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                           {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                           "tiltPageTurn", StrId::STR_CAT_CONTROLS));
        break;
      }
    }
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  return v;
}
