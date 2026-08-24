#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "activities/settings/SettingsActivity.h"
#include "util/BoundMenuLabels.h"
#include "util/DictionaryRegistry.h"

// Labels for the three bindings that share LONG_PRESS_MENU_FUNCTION (Long-press Menu,
// Menu Hold, Double-Click Power). ENUM settings persist by INDEX, so entry i MUST be the
// label for enum value i; walking the values in order is what guarantees that, rather than
// a hand-written list that could be reordered and silently reinterpret every saved binding.
// Binding values that keep their slot but are no longer offered. Shared by all three
// binding rows so one of them cannot quietly go on offering a retired action.
inline std::vector<uint8_t> retiredBoundFunctions() {
  return {CrossPointSettings::LP_MENU_SELECT_CHAPTER, CrossPointSettings::LP_MENU_GO_TO_PERCENT,
          CrossPointSettings::LP_MENU_TEXT_SETTINGS};
}

inline std::vector<StrId> boundFunctionLabels() {
  std::vector<StrId> labels;
  labels.reserve(CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT);
  for (uint8_t value = 0; value < CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT; value++) {
    labels.push_back(boundMenuActionLabel(value));
  }
  return labels;
}

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
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
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated — see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// The static list is constructed exactly once (master's optimization, #1086 +
// #1636) so the per-entry SettingInfo cost is paid once; every call then copies
// it. When an SdCardFontRegistry is supplied AND has SD card fonts installed,
// the font-family entry is replaced in that copy with a registry-aware version.
// The font-size entry is always rebuilt, since its options are point sizes read
// from the active family rather than a fixed enum.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    // DARK is retired (see SLEEP_SCREEN_MODE): the label stays for index alignment, and
    // withHiddenEnumValues() below keeps it out of the picker.
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    // BLANK and FREEZE are retired (see SLEEP_SCREEN_MODE). Their labels stay so the array
    // remains index-aligned with the enum — the value is what persists — but both are
    // listed in withHiddenEnumValues() below and never reach the picker.
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::STATS_DASHBOARD] = StrId::STR_STATS_DASHBOARD;
    sleepScreenValues[CrossPointSettings::FREEZE] = StrId::STR_FREEZE;
    sleepScreenValues[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;

    // The list is appended one entry at a time instead of being written as a single
    // braced initializer. A braced list materializes ALL entries as one temporary array
    // in this frame before the vector copies them; at 86 entries that array alone was
    // ~15.5 KB and overflowed the 16 KB Arduino loop task stack, panicking with a
    // "Stack protection fault" during boot before anything painted. push_back keeps
    // exactly one SettingInfo temporary alive at a time.
    std::vector<SettingInfo> v;
    v.reserve(88);
    // --- Display ---
    v.push_back(
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY)
            .withHiddenEnumValues({CrossPointSettings::DARK, CrossPointSettings::BLANK, CrossPointSettings::FREEZE}));

    v.push_back(
        SettingInfo::Enum(StrId::STR_SELECTION_STYLE, &CrossPointSettings::selectionStyle,
                          {StrId::STR_SELECTION_SOLID, StrId::STR_SELECTION_BRACKETS, StrId::STR_SELECTION_CARET},
                          "selectionStyle", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                  {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                  {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                  "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_SLEEP_IMAGE_FILENAME, &CrossPointSettings::showSleepImageFilename,
                                    "showSleepImageFilename", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_WAKE_STRAIGHT_TO_BOOK, &CrossPointSettings::wakeStraightToBook,
                                    "wakeStraightToBook", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_SLEEP_FAVORITE_BADGE, &CrossPointSettings::showSleepFavoriteBadge,
                                    "showSleepFavoriteBadge", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_SLEEP_WALLPAPER_POSITION,
                                    &CrossPointSettings::showSleepWallpaperPosition, "showSleepWallpaperPosition",
                                    StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                                  StrId::STR_CAT_DISPLAY));

    // "Never" (index 5) was dropped: an X3 left without a periodic cleanup ghosts badly,
    // and upstream never offered it. A settings file still holding 5 clamps back to the
    // 15-page default, because load() falls back to the field default for any index past
    // the option list.
    v.push_back(SettingInfo::Enum(
        StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
        {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30,
         StrId::STR_REFRESH_NEVER},
        "refreshFrequency", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                    StrId::STR_CAT_DISPLAY));

    // Frontlight. Dropped below on a board the SDK reports no frontlight for,
    // so these four rows only ever appear on hardware that has one.
    v.push_back(SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn",
                                    StrId::STR_CAT_DISPLAY));

    // Step 5 rather than 1: the panel's perceived brightness moves in coarse
    // jumps, and a 0-100 row stepped by one takes twenty presses to cross.
    v.push_back(SettingInfo::Value(StrId::STR_FRONTLIGHT_BRIGHTNESS, &CrossPointSettings::frontlightBrightness,
                                   {0, 100, 5}, "frontlightBrightness", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Value(StrId::STR_FRONTLIGHT_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5},
                                   "frontlightWarmth", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Toggle(StrId::STR_FRONTLIGHT_RESTORE_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                                    "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY));

    v.push_back(SettingInfo::Enum(StrId::STR_AUTHOR_DISPLAY, &CrossPointSettings::authorDisplay,
                                  {StrId::STR_AUTHOR_INITIALS, StrId::STR_AUTHOR_FULL_NAME}, "authorDisplay",
                                  StrId::STR_CAT_DISPLAY));

    // Free-text footer shown on the wake/unlock screen bottom banner.
    v.push_back(SettingInfo::String(StrId::STR_SLEEP_FOOTER_TEXT, &SETTINGS.customFooter[0],
                                    sizeof(SETTINGS.customFooter), "customFooter", StrId::STR_CAT_DISPLAY));

    // --- Reader ---
    // Built-in font-family entry. Replaced per-call with a registry-aware
    // version when SD fonts are installed.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                                  {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
                    .withTextSettings());

    // Placeholder: the selectable sizes depend on the active font family, so
    // this entry is always replaced by buildFontSizeSetting() below. It only
    // fixes the setting's position in the Reader category.
    v.push_back(
        SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings());

    // Granular line spacing as a percentage of natural line height (restored from
    // old lector; supersedes the coarse Tight/Normal/Wide enum).
    v.push_back(SettingInfo::Value(
                    StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacingPercent,
                    {CrossPointSettings::MIN_LINE_SPACING_PERCENT, CrossPointSettings::MAX_LINE_SPACING_PERCENT, 5},
                    "lineSpacingPercent", StrId::STR_CAT_READER)
                    .withTextSettings());

    // The web settings API can write this key straight into the field, so the mode's own
    // consequences (All Sides carries the horizontal margin onto every side and turns
    // Dynamic Margins off) are re-applied by CrossPointSettings::normalizeMargins().
    v.push_back(
        SettingInfo::Enum(StrId::STR_LINK_MARGINS, &CrossPointSettings::marginLinkMode,
                          {StrId::STR_MARGIN_LINK_OFF, StrId::STR_MARGIN_LINK_TOP_BOTTOM, StrId::STR_MARGIN_LINK_ALL},
                          "marginLinkMode", StrId::STR_CAT_READER)
            .withTextSettings());

    v.push_back(SettingInfo::Value(StrId::STR_HORIZONTAL_MARGIN, &CrossPointSettings::screenMargin,
                                   {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX, 5},
                                   "screenMargin", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN_TOP, &CrossPointSettings::screenMarginTop,
                                   {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX, 5},
                                   "screenMarginTop", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN_BOTTOM, &CrossPointSettings::screenMarginBottom,
                                   {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX, 5},
                                   "screenMarginBottom", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Enum(
                    StrId::STR_DYNAMIC_MARGINS, &CrossPointSettings::dynamicMargins,
                    {StrId::STR_DYNAMIC_MARGINS_OFF, StrId::STR_DYNAMIC_MARGINS_10, StrId::STR_DYNAMIC_MARGINS_20},
                    "dynamicMargins", StrId::STR_CAT_READER)
                    .withTextSettings());

    // First-line indent (restored old-lector model): Book (respect CSS) vs Custom %.
    v.push_back(SettingInfo::Enum(StrId::STR_FIRST_LINE_INDENT, &CrossPointSettings::firstLineIndentMode,
                                  {StrId::STR_INDENT_BOOK, StrId::STR_INDENT_PERCENT}, "firstLineIndentMode",
                                  StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Value(StrId::STR_FIRST_LINE_INDENT_PERCENT, &CrossPointSettings::firstLineIndentPercent,
                                   {0, CrossPointSettings::MAX_FIRST_LINE_INDENT_PERCENT, 5}, "firstLineIndentPercent",
                                   StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                                  {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE},
                                  "paragraphAlignment", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_TEXT_STYLE, &CrossPointSettings::embeddedTextStyle,
                                    "embeddedStyle", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_LAYOUT_STYLE, &CrossPointSettings::embeddedLayoutStyle,
                                    "embeddedLayoutStyle", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                    "focusReadingEnabled", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_GUIDE_DOTS, &CrossPointSettings::guideDotsEnabled, "guideDotsEnabled",
                                    StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_HIDDEN_DOTS, &CrossPointSettings::guideDotsHidden, "guideDotsHidden",
                                    StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_DEBUG_BORDERS, &CrossPointSettings::debugBorders, "debugBorders",
                                    StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                                    "hyphenationEnabled", StrId::STR_CAT_READER)
                    .withTextSettings());

    v.push_back(SettingInfo::Enum(
        StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
        "orientation", StrId::STR_CAT_READER));

    v.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                    "extraParagraphSpacing", StrId::STR_CAT_READER)
                    .withTextSettings());

    // Retired in 0.8.2: the granular paragraph gap (% of line height) duplicated what
    // the Extra Paragraph Spacing toggle above already does. The field and its render
    // spec entry are kept (old caches and sidecars still carry it) but it is pinned to
    // 0 and no longer editable, here or in the in-book Layout tab. Restore this entry
    // to bring the control back.
    // SettingInfo::Value(StrId::STR_PARAGRAPH_SPACING, &CrossPointSettings::paragraphSpacing,
    //                    {0, CrossPointSettings::MAX_PARAGRAPH_SPACING, 10}, "paragraphSpacing",
    //                    StrId::STR_CAT_READER)
    //     .withTextSettings(),
    v.push_back(SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                                    StrId::STR_CAT_READER)
                    .withTextSettings());

    // Defaults for the three per-book looks that used to be reachable only from the
    // in-book menu. Changing one here sets what the NEXT freshly opened book starts
    // with; a book that already has its own override keeps its own value, and the
    // in-book menu still overrides any of them per book.
    v.push_back(SettingInfo::Enum(StrId::STR_PARAGRAPH_NUMBERS, &CrossPointSettings::paragraphNumbering,
                                  {StrId::STR_PARA_NUM_OFF, StrId::STR_PARA_NUM_CHAPTER}, "paragraphNumbering",
                                  StrId::STR_CAT_READER));

    v.push_back(SettingInfo::Enum(StrId::STR_PARAGRAPH_NUMBER_SIZE, &CrossPointSettings::paragraphNumberSize,
                                  {StrId::STR_PARA_NUM_SIZE_SMALL, StrId::STR_PARA_NUM_SIZE_DOUBLE},
                                  "paragraphNumberSize", StrId::STR_CAT_READER));

    // Which tab the in-book menu lands on. Labels are the tab bar's own strings, and
    // the list is index-aligned with CrossPointSettings::BOOK_MENU_TAB because ENUM
    // settings persist by index.
    {
      std::vector<StrId> bookMenuTabValues(CrossPointSettings::BOOK_MENU_TAB_COUNT);
      bookMenuTabValues[CrossPointSettings::BOOK_MENU_TAB_NAVIGATE] = StrId::STR_SEC_NAVIGATE;
      bookMenuTabValues[CrossPointSettings::BOOK_MENU_TAB_THIS_BOOK] = StrId::STR_SEC_THIS_BOOK;
      bookMenuTabValues[CrossPointSettings::BOOK_MENU_TAB_LOOK] = StrId::STR_SEC_LOOK;
      bookMenuTabValues[CrossPointSettings::BOOK_MENU_TAB_DEVICE] = StrId::STR_SEC_DEVICE;
      bookMenuTabValues[CrossPointSettings::BOOK_MENU_TAB_SLEEP] = StrId::STR_SEC_SLEEP_SCREEN;
      v.push_back(SettingInfo::Enum(StrId::STR_BOOK_MENU_TAB, &CrossPointSettings::bookMenuTab,
                                    std::move(bookMenuTabValues), "bookMenuTab", StrId::STR_CAT_READER));
    }

    v.push_back(SettingInfo::Toggle(StrId::STR_PAPERBACK_LOOK, &CrossPointSettings::paperbackLookBody,
                                    "paperbackLookBody", StrId::STR_CAT_READER));

    v.push_back(SettingInfo::Toggle(StrId::STR_PAPERBACK_STATUS, &CrossPointSettings::paperbackLookStatus,
                                    "paperbackLookStatus", StrId::STR_CAT_READER));

    // Night mode = inverted output polarity on the reading surfaces only
    // (EPUB/TXT/XTC; ActivityManager resolves the polarity per render).
    // Reader category, since it does not affect the rest of the UI.
    v.push_back(SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                                    StrId::STR_CAT_READER));

    // --- Controls ---
    v.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                  {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                                  StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION,
                                    &CrossPointSettings::frontButtonFollowOrientation, "frontButtonFollowOrientation",
                                    StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                                  boundFunctionLabels(), "longPressMenuFunction", StrId::STR_CAT_CONTROLS)
                    .withHiddenEnumValues(retiredBoundFunctions()));

    v.push_back(SettingInfo::Enum(StrId::STR_MENU_HOLD, &CrossPointSettings::menuHoldFunction, boundFunctionLabels(),
                                  "menuHoldFunction", StrId::STR_CAT_CONTROLS)
                    .withHiddenEnumValues(retiredBoundFunctions()));

    v.push_back(SettingInfo::Enum(StrId::STR_WAKE_HOLD, &CrossPointSettings::wakeHold,
                                  {StrId::STR_WAKE_HOLD_NORMAL, StrId::STR_WAKE_HOLD_FAST}, "wakeHold",
                                  StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Enum(StrId::STR_DOUBLE_CLICK_POWER, &CrossPointSettings::doubleClickPowerFunction,
                                  boundFunctionLabels(), "doubleClickPowerFunction", StrId::STR_CAT_CONTROLS)
                    .withHiddenEnumValues(retiredBoundFunctions()));

    v.push_back(SettingInfo::Enum(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE,
                                   StrId::STR_STATE_INVERTED_TAP},
                                  "touchReaderControls", StrId::STR_CAT_CONTROLS));

    // Persisted under CrossPoint's legacy "tapForReaderMenu" key: the old values
    // line up (0 = Off, 1 = Tap), so a settings file carries over either way.
    v.push_back(SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP},
                                  "tapForReaderMenu", StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Enum(
        StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
        {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES},
        "shortPwrBtn", StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                                    "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                                    "backShortToFileBrowser", StrId::STR_CAT_CONTROLS));

    v.push_back(SettingInfo::Enum(StrId::STR_HOME_BACK_ACTION, &CrossPointSettings::homeBackAction,
                                  {StrId::STR_NONE_OPT, StrId::STR_RESUME, StrId::STR_READING_STATS}, "homeBackAction",
                                  StrId::STR_CAT_CONTROLS));

    // --- System ---
    v.push_back(SettingInfo::Value(
        StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
        {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
        "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));

    // How this reader introduces itself during Nearby Position Sync. Left empty
    // it falls back to a generated "Lector-XXXX" from the MAC.
    v.push_back(SettingInfo::String(StrId::STR_DEVICE_NAME, &SETTINGS.deviceName[0], sizeof(SETTINGS.deviceName),
                                    "deviceName", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Enum(StrId::STR_OPEN_BOOK_ON_BOOT, &CrossPointSettings::bootBookMode,
                                  {StrId::STR_BOOT_BOOK_OFF, StrId::STR_BOOT_BOOK_LAST, StrId::STR_BOOT_BOOK_RANDOM},
                                  "bootBookMode", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles,
                                    "showHiddenFiles", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Enum(StrId::STR_BOOK_BROWSER_ORDER, &CrossPointSettings::bookBrowserOrder,
                                  {StrId::STR_BOOK_ORDER_ALPHABETICAL, StrId::STR_BOOK_ORDER_RANDOM,
                                   StrId::STR_BOOK_ORDER_RECENTLY_ADDED, StrId::STR_BOOK_ORDER_LAST_READ},
                                  "bookBrowserOrder", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS,
                                    &CrossPointSettings::removeReadBooksFromRecents, "removeReadBooksFromRecents",
                                    StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                                    "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_OPENED_TO_RECENTS, &CrossPointSettings::moveOpenedToRecentsFolder,
                                    "moveOpenedToRecentsFolder", StrId::STR_CAT_SYSTEM));

    // OPDS download folder: persisted + web-exposed, but category-less so it
    // is hidden from the on-device Settings screen (edited via OPDS UI).
    v.push_back(SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                                    sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"));

    // OPDS download filename format: persisted + web-exposed, category-less so it
    // is hidden from the on-device Settings screen (cycled from the OPDS UI).
    v.push_back(SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                                  {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                                  "opdsFilenameFormat"));

    // Reading statistics: whether to track at all, and how long a page can sit
    // untouched before the session stops counting it as reading time.
    v.push_back(SettingInfo::Toggle(StrId::STR_TRACK_READING_STATS, &CrossPointSettings::readingStatsEnabled,
                                    "readingStatsEnabled", StrId::STR_CAT_SYSTEM));

    v.push_back(SettingInfo::Value(
        StrId::STR_READING_IDLE_LIMIT, &CrossPointSettings::readingStatsIdleUnits,
        {CrossPointSettings::MIN_READING_STATS_IDLE_UNITS, CrossPointSettings::MAX_READING_STATS_IDLE_UNITS, 1},
        "readingStatsIdleUnits", StrId::STR_CAT_SYSTEM));

    // Measurement, off by default and free while it is off. On, the device times every
    // refresh, draws the previous one's cost in a corner of the screen, and writes a CSV
    // to /perf on the card for reading on a computer. It exists because the device has no
    // serial console in a reader's hands, so the only honest way to judge a speed change
    // is to have the device report its own numbers. See docs/perf-measurement.md.
    v.push_back(SettingInfo::Toggle(StrId::STR_PERF_TIMINGS, &CrossPointSettings::showTimings, "showTimings",
                                    StrId::STR_CAT_SYSTEM));

    // The panel's cheapest partial waveform on page turns and menu moves. On by default
    // on the one board validated for it; the row is here so a panel that turns out to
    // ghost can be put back on the vendor sequence without a reflash. See
    // CrossPointSettings::fastPageTurns.
    v.push_back(SettingInfo::Toggle(StrId::STR_FAST_PAGE_TURNS, &CrossPointSettings::fastPageTurns, "fastPageTurns",
                                    StrId::STR_CAT_SYSTEM));

    // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
    v.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
        [](const std::string& v) {
          KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
          KOREADER_STORE.saveToFile();
        },
        "koUsername", StrId::STR_KOREADER_SYNC));

    v.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
        [](const std::string& v) {
          KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
          KOREADER_STORE.saveToFile();
        },
        "koPassword", StrId::STR_KOREADER_SYNC));

    v.push_back(SettingInfo::DynamicString(
        StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
        [](const std::string& v) {
          KOREADER_STORE.setServerUrl(v);
          KOREADER_STORE.saveToFile();
        },
        "koServerUrl", StrId::STR_KOREADER_SYNC));

    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
        [](uint8_t v) {
          KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
          KOREADER_STORE.saveToFile();
        },
        "koMatchMethod", StrId::STR_KOREADER_SYNC));

    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
        [](uint8_t v) {
          KOREADER_STORE.setSendMetadata(v != 0);
          KOREADER_STORE.saveToFile();
        },
        "koSendMetadata", StrId::STR_KOREADER_SYNC));

    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
        [](uint8_t v) {
          KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
          KOREADER_STORE.saveToFile();
        },
        "koSyncBehavior", StrId::STR_KOREADER_SYNC));

    // --- Per-item status bar model (v2). Position enums share the same 7-value
    // list {Off, TL, TC, TR, BL, BC, BR}; the device UI (StatusBarSettingsActivity)
    // renders these as the [XX] position popup. ---
    v.push_back(SettingInfo::Toggle(StrId::STR_STATUS_BAR, &CrossPointSettings::sbEnabled, "sbEnabled",
                                    StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_BATTERY, &CrossPointSettings::sbBatteryPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbBatteryPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::sbClockPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbClockPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::sbTitlePos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbTitlePos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Enum(StrId::STR_TITLE_SOURCE, &CrossPointSettings::sbTitleSource,
                                  {StrId::STR_BOOK, StrId::STR_CHAPTER}, "sbTitleSource",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Toggle(StrId::STR_TRUNCATE_TITLE, &CrossPointSettings::sbTitleTruncate, "sbTitleTruncate",
                                    StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_PAGE_IN_CHAPTER, &CrossPointSettings::sbPagePos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbPagePos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Enum(StrId::STR_PAGE_FORMAT, &CrossPointSettings::sbPageFormat,
                                  {StrId::STR_PAGE_FRACTION, StrId::STR_PAGE_LEFT}, "sbPageFormat",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_BOOK_PERCENT, &CrossPointSettings::sbBookPctPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbBookPctPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_CHAPTER_PERCENT, &CrossPointSettings::sbChapterPctPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbChapterPctPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_CHAPTER_NUMBER, &CrossPointSettings::sbChapterNumPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbChapterNumPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_SESSION_PAGES, &CrossPointSettings::sbSessionPagesPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbSessionPagesPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(
        SettingInfo::Enum(StrId::STR_PARA_PAGES, &CrossPointSettings::sbParaPagesPos,
                          {StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
                           StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR},
                          "sbParaPagesPos", StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Enum(StrId::STR_BOOK_BAR, &CrossPointSettings::sbBookBar,
                                  {StrId::STR_STATE_OFF, StrId::STR_TOP, StrId::STR_BOTTOM}, "sbBookBar",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Enum(StrId::STR_CHAPTER_BAR, &CrossPointSettings::sbChapterBar,
                                  {StrId::STR_STATE_OFF, StrId::STR_TOP, StrId::STR_BOTTOM}, "sbChapterBar",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Enum(StrId::STR_BAR_THICKNESS, &CrossPointSettings::sbBarThickness,
                                  {StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_FAT}, "sbBarThickness",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Toggle(StrId::STR_FLOATING_BAR, &CrossPointSettings::sbFloatingBar, "sbFloatingBar",
                                    StrId::STR_CUSTOMISE_STATUS_BAR));

    v.push_back(SettingInfo::Toggle(StrId::STR_BAR_OUTLINE, &CrossPointSettings::sbBarOutline, "sbBarOutline",
                                    StrId::STR_CUSTOMISE_STATUS_BAR));

    // Keeps the Book Bar / Chapter Bar edges drawing while the status bar itself is
    // hidden, at its own thickness. Ignored while the status bar is on.
    v.push_back(
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::sbOffBar,
                          {StrId::STR_STATE_OFF, StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_FAT},
                          "sbOffBar", StrId::STR_CUSTOMISE_STATUS_BAR));

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
    return v;
  }();

  std::vector<SettingInfo> v = baseList;
  // The status-bar clock reads the RTC, which only the X3 carries. HalClock does have
  // a system-clock fallback for boards without one, but deep sleep here is a full chip
  // reset, so that clock would be lost every time the reader sleeps — a clock that is
  // right only until you close the cover is worse than no clock. So the item stays
  // RTC-only, and its row is dropped on a board that cannot ever show it rather than
  // offering a setting that does nothing.
  //
  // Filtered in this per-call copy rather than in the static baseList: the static is
  // built on first use, which is not guaranteed to be after HalClock::begin().
  // No frontlight on this board: drop the four light rows rather than offer
  // controls that move nothing. Filtered in this per-call copy rather than in
  // the static baseList because the static is built on first use, which is not
  // guaranteed to be after HalFrontlight::begin().
  if (!Frontlight.present()) {
    static constexpr StrId FRONTLIGHT_ROWS[] = {StrId::STR_FRONTLIGHT, StrId::STR_FRONTLIGHT_BRIGHTNESS,
                                                StrId::STR_FRONTLIGHT_WARMTH, StrId::STR_FRONTLIGHT_RESTORE_ON_WAKE};
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return std::find(std::begin(FRONTLIGHT_ROWS), std::end(FRONTLIGHT_ROWS), s.nameId) !=
                                    std::end(FRONTLIGHT_ROWS);
                           }),
            v.end());
  } else if (!Frontlight.hasColorTemperature()) {
    // Single-colour light: brightness applies, warmth does not.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_FRONTLIGHT_WARMTH; }),
            v.end());
  }
  if (!halClock.isAvailable()) {
    v.erase(std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_CLOCK; }),
            v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
