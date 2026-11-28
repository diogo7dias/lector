#include "CrossPointSettings.h"

#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "ReaderPresetMigration.h"
#include "SettingsList.h"
#include "activities/reader/ReaderTouchZones.h"
#include "fontIds.h"
#include "frontlight/FrontlightBootPolicy.h"
#include "sleep/WakeFacePolicy.h"
#include "util/BoundActionScope.h"
#include "util/SleepTimeoutGuard.h"

namespace {

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  // One entry at a time, not getSettingsList(): holding ~90 entries failed (bad_alloc)
  // saving after a WiFi join with 17 KB of heap left.
  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  };
  forEachBaseSetting(visit);
  doc["sleepFaceNumbering"] = 1;  // see wake_face::sleepFaceFromFile

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["fontSize"] = fontPointSize;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Menu Pop-up membership — a uint16_t mask, so the generic uint8_t loop cannot carry it.
  doc["popupItems"] = popupItems;
  // Hold Wallpaper — no longer a SettingsList row (it lives in the in-book menu), so the
  // generic loop no longer sees it.
  doc["wallpaperRotationPaused"] = wallpaperRotationPaused;
  doc["calmMode"] = calmMode;
  // Marks the one-time 0.8.2 reading-defaults migration in fromJson as done.
  doc["readingDefaults0820"] = true;
  // Marks the one-time Touch Sleep Off migration in migrateFromJson as done.
  doc["touchSleepOff0410"] = true;
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        return;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, info.enumCount ? info.enumCount : (uint8_t)info.enumValues.size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  };
  forEachBaseSetting(visit);

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  // Hold Wallpaper — see toJson. Loaded by hand for the same reason.
  wallpaperRotationPaused = (doc["wallpaperRotationPaused"] | (uint8_t)0) ? 1 : 0;
  calmMode = (doc["calmMode"] | (uint8_t)0) ? 1 : 0;

  // Menu Pop-up membership. Masked to the defined actions so a hand-edited or
  // future-version settings file cannot set a bit no builder knows how to draw, and
  // trimmed to POPUP_ITEM_MAX so a file claiming twenty rows cannot build a pop-up
  // taller than the panel.
  {
    // Widened to 32 bits when Delete Wallpaper became the first pop-up row past bit 15.
    // No migration: the mask persists as a plain number and every existing bit keeps its
    // position, so a file written by an older build reads back unchanged.
    uint32_t storedPopupItems = doc["popupItems"] | (uint32_t)0;
    uint32_t validMask = 0;
    for (const uint8_t fn : POPUP_ITEM_FUNCTIONS) validMask |= static_cast<uint32_t>(1u << fn);
    storedPopupItems &= validMask;
    popupItems = 0;
    uint8_t kept = 0;
    for (const uint8_t fn : POPUP_ITEM_FUNCTIONS) {
      if (!((storedPopupItems >> fn) & 1u)) continue;
      if (kept >= POPUP_ITEM_MAX) break;
      popupItems |= static_cast<uint32_t>(1u << fn);
      kept++;
    }
    if (popupItems != storedPopupItems) needsResave = true;
  }

  fontPointSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';

  // Dictionary folder name — uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));

  // "language" from builds before 2026-11-25 is ignored: the UI is English only, and the
  // key drops out at the next save. Every other setting loads as it always did.

  needsResave = migrateFromJson(doc) || needsResave;

  // A hand-edited or partly-migrated file can hold sides that disagree with its link
  // mode; the mode is the statement of record, so it wins.
  normalizeMargins();

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

bool CrossPointSettings::migrateFromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  // Time zone: 0.58.0 stored a fixed UTC offset (clockUtcOffsetQ) and a city from a list of ten
  // (clockTimeZone, 0 = use the offset); CrossPoint 1.x stored the offset alone. A file with no
  // valid clockTimezone takes the zone those two keys meant, so it keeps showing the same local
  // time, and the resave drops them.
  if ((doc["clockTimezone"] | (uint8_t)255) >= clock_zones::COUNT) {
    clockTimezone = clock_zones::fromLegacy(doc["clockTimeZone"] | (uint8_t)0, doc["clockUtcOffsetQ"] | (uint8_t)48);
    LOG_INF("CPS", "Time zone from the old keys: %s", clock_zones::TABLE[clockTimezone].name);
    needsResave = true;
  }

  // Title Source left the settings list (see StatusBarBlock::foldTitleSource): a file that
  // chose Book moves that title onto the Book Title item, once, and is written back.
  if ((doc["sbTitleSource"] | (uint8_t)SB_TITLE_CHAPTER) == SB_TITLE_BOOK) {
    sbTitleSource = SB_TITLE_BOOK;
    setStatusBar(statusBar());
    needsResave = true;
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue = doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN;
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // "Open Book on Boot" replaced the old "Open Random Book on Boot" toggle.
  // A settings file that only carries the toggle keeps the behaviour it described: on
  // means Random, off means the ordinary routing.
  if (doc["bootBookMode"].isNull() && !doc["openRandomRecentOnBoot"].isNull()) {
    bootBookMode = (doc["openRandomRecentOnBoot"] | (uint8_t)0) ? BOOT_BOOK_RANDOM : BOOT_BOOK_OFF;
    needsResave = true;
  }
  // "Off" is retired: an unlock always lands on a book. A file still holding it moves to
  // Last Book, which is what Off did whenever the last sleep came from the reader anyway.
  if (bootBookMode == BOOT_BOOK_OFF) {
    bootBookMode = BOOT_BOOK_LAST;
    needsResave = true;
  }

  // "File Browser Order" grew from a two-way alphabetical/random pick into a four-way one.
  // The old key's two values mean the same thing under the new one, so a settings file that
  // only carries it keeps the order it was showing.
  if (doc["bookBrowserOrder"].isNull() && !doc["bookBrowserRandomOrder"].isNull()) {
    bookBrowserOrder = (doc["bookBrowserRandomOrder"] | (uint8_t)0) ? BOOK_ORDER_RANDOM : BOOK_ORDER_ALPHABETICAL;
    needsResave = true;
  }

  // Margins model, two changes deep: "uniformMargins" became the "verticalMarginsLinked"
  // switch, which became the three link modes. Same rule as a saved preset's; written
  // once, and the resave drops the old keys.
  if (reader_preset_migration::migrateMarginLink(reader_preset_migration::readLegacyKeys(doc), screenMargin,
                                                 screenMarginTop, screenMarginBottom, dynamicMargins, marginLinkMode)) {
    needsResave = true;
  }

  // Lean typography: the indent counts spaces (it was Book or a percent of the column),
  // Character Spacing counts whole pixels under a new key (it was half pixels), and Book
  // Styling is one switch (it was text and layout). Same rule as a saved preset's. The
  // pre-0.8.2 defaults block below still wins over the indent this reads.
  const auto leanKeys = reader_preset_migration::readLegacyKeys(doc);
  if (!leanKeys.hasParagraphIndentSpaces) {
    reader_preset_migration::migrateLeanTypography(leanKeys, paragraphIndentSpaces, embeddedStyle, characterSpacing);
    // Placeholder and Suppress did nothing from 2026-08-11 until the Images row came back.
    imageRendering = IMAGES_DISPLAY;
    needsResave = true;
  }
  // Word Spacing and the side margin moved to coarser steps, so a saved value lands on the grid.
  const uint8_t snappedWordSpacing =
      reader_defaults::snapToStep(wordSpacing, reader_defaults::MIN_WORD_SPACING, reader_defaults::MAX_WORD_SPACING,
                                  reader_defaults::WORD_SPACING_STEP);
  const uint8_t snappedMargin =
      reader_defaults::snapToStep(screenMargin, reader_defaults::MIN_SCREEN_MARGIN, reader_defaults::MAX_SCREEN_MARGIN,
                                  reader_defaults::SCREEN_MARGIN_STEP);
  if (snappedWordSpacing != wordSpacing || snappedMargin != screenMargin) needsResave = true;
  wordSpacing = snappedWordSpacing;
  screenMargin = snappedMargin;

  // Touch page turning used to be one combined mode under "touchReaderControls" (0 Off, 1 Tap,
  // 2 Swipe, 3 Inverted Tap). It is now that key as an on/off switch plus a gesture per page
  // direction; a file that has neither gesture key yet is split once. The generic load above
  // already folded a stored 2 or 3 to the On default.
  if (doc["pageTurnGesture"].isNull() && doc["previousPageGesture"].isNull() &&
      doc["touchReaderControls"].is<uint8_t>()) {
    const auto legacy = reader_touch::migrateLegacyMode(doc["touchReaderControls"].as<uint8_t>());
    if (legacy.valid) {
      touchReaderControls = legacy.on;
      pageTurnGesture = previousPageGesture = legacy.gesture;
      needsResave = true;
    }
  }

  // Sleep face numbers follow CrossPoint's. A file without the stamp is a lector file from
  // before that; see wake_face::sleepFaceFromFile for what it keeps. Read from the raw
  // document: the generic loader above clamps an unknown value to the default first.
  {
    const bool numbered = doc["sleepFaceNumbering"].is<uint8_t>();
    const uint8_t face = wake_face::sleepFaceFromFile(doc["sleepScreen"] | sleepScreen, numbered);
    if (face != sleepScreen || !numbered) {
      sleepScreen = face;
      needsResave = true;
    }
  }

  // Bindings still set to a retired action (Select Chapter, Go to Percent, Text Settings)
  // fall back to Disabled. The pickers can no longer produce those values, but a settings file written
  // by 0.20.0 can still name one, and it would go on working invisibly.
  for (uint8_t CrossPointSettings::* binding :
       {&CrossPointSettings::longPressMenuFunction, &CrossPointSettings::menuHoldFunction}) {
    if (s.*binding == LP_MENU_SELECT_CHAPTER || s.*binding == LP_MENU_GO_TO_PERCENT ||
        s.*binding == LP_MENU_TEXT_SETTINGS) {
      s.*binding = LP_MENU_DISABLED;
      needsResave = true;
    }
  }

  // The power button's two old settings folded into its per-button bindings (0.29).
  // shortPwrBtn offered five choices and doubleClickPowerFunction the full action list;
  // both are now three of the power button's six bindings, so the stored values are
  // copied across once and the old keys stop being written.
  //
  // Copied into BOTH contexts, because neither old setting knew about one: they applied
  // wherever the reader was. Footnotes is the exception, being the one migrated value that
  // needs an open book, so outside a book it lands on Disabled instead.
  // Both old keys are read straight from the document: their fields are gone, so the
  // settings-list walk above no longer loads them.
  if (!doc["btnUiPowerSingle"].is<uint8_t>()) {
    // CrossPoint's SHORT_PWRBTN enum: 0 Ignore, 1 Sleep, 2 Page turn, 3 Force refresh,
    // 4 Footnotes, 5 Confirm, 6 Clipping, 7 Bookmark, 8 Sync, 9 Reader menu.
    const uint8_t single = [&]() -> uint8_t {
      switch (doc["shortPwrBtn"] | (uint8_t)0) {
        case 1:
          return LP_MENU_SLEEP;
        case 2:
          return LP_MENU_PAGE_NEXT;
        case 3:
          return LP_MENU_FORCE_REFRESH;
        case 4:
          return LP_MENU_FOOTNOTES;
        case 5:
          return LP_MENU_CONFIRM;
        case 6:
          return LP_MENU_GRAB_QUOTE;
        case 7:
          return LP_MENU_BOOKMARK;
        case 8:
          return LP_MENU_KOSYNC;
        case 9:
          return LP_MENU_OPEN_READER_MENU;
        default:
          return LP_MENU_DISABLED;
      }
    }();
    const uint8_t doubleClick = doc["doubleClickPowerFunction"] | (uint8_t)LP_MENU_DISABLED;
    btnBookPowerSingle = single;
    btnUiPowerSingle = bound_action::allowedOutsideBook(single) ? single : LP_MENU_DISABLED;
    btnBookPowerDouble = doubleClick;
    btnUiPowerDouble = bound_action::allowedOutsideBook(doubleClick) ? doubleClick : LP_MENU_DISABLED;
    needsResave = true;
  }

  // Retire stats only after legacy power bindings above have been copied across.
  if (homeBackAction == HOME_BACK_STATS) {
    homeBackAction = HOME_BACK_RESUME;
    needsResave = true;
  }
  const auto retireStatsBinding = [&needsResave](uint8_t& binding) {
    if (binding == LP_MENU_READING_STATS) {
      binding = LP_MENU_DISABLED;
      needsResave = true;
    }
  };
  retireStatsBinding(longPressMenuFunction);
  retireStatsBinding(menuHoldFunction);
  forEachBindingField([&](const BindingField field) { retireStatsBinding(this->*field); });

  // Reader font size — an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  if (fontPointSize <= LEGACY_FONT_SIZE_MAX) {
    fontPointSize = foldLegacyReaderFontSize(fontPointSize);  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }

  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = CHAREINK;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  // Reading defaults changed in 0.8.1: a first-line indent, air between paragraphs,
  // and paragraph numbers per chapter. A settings file written before that carries the
  // old zeros, so it would keep the old look forever and read as a broken setting. The
  // marker makes this run exactly once; changing any of the three afterwards sticks.
  // 0.8.2 re-seeds the same block: the separate paragraph-spacing percentage is gone, so
  // the gap is the Extra Paragraph Spacing toggle alone and it is forced back on. A file
  // written by 0.8.1 carries the old 50% and must not keep a double gap.
  if (!(doc["readingDefaults0820"] | false)) {
    // Same values the per-book sidecar upgrade applies, from the same constants, so
    // global and per-book can never drift apart.
    extraParagraphSpacing = reader_defaults::EXTRA_PARAGRAPH_SPACING;
    paragraphIndentSpaces = reader_defaults::PARAGRAPH_INDENT_SPACES;
    paragraphNumbering = PARA_NUM_CHAPTER;
    needsResave = true;
  }
  // Touch Sleep shipped On (30 s) in 0.40.0 and left touch dead after a pause in reading.
  // The default is Off now; a file written before carries the old default, so it is reset
  // once. Turning it back on afterwards sticks.
  if (!(doc["touchSleepOff0410"] | false)) {
    touchSleep = 0;
    needsResave = true;
  }

  return needsResave;
}

float CrossPointSettings::getReaderLineCompression() const { return readerLineCompression(lineSpacingPercent); }

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  // The floor first: with Sleep bound to nothing, auto-sleep is the only thing left that
  // puts the device down, so Never and any long timeout are capped. The stored setting is
  // untouched and returns the moment Sleep is bound again.
  const uint8_t requested =
      sleep_guard::effectiveMinutes(sleepTimeoutMinutes, anySleepBinding(), SLEEP_TIMEOUT_NEVER_MINUTES);
  if (requested >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(requested, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

const char* CrossPointSettings::getEffectiveDeviceName() const {
  if (deviceName[0] != '\0') return deviceName;

  // Built once and kept: the MAC does not change while the device is running,
  // and callers hand this straight to a packet encoder or a renderer.
  static char generated[sizeof(deviceName)] = "";
  if (generated[0] == '\0') {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
      std::snprintf(generated, sizeof(generated), "Lector-%02X%02X", mac[4], mac[5]);
    } else {
      std::snprintf(generated, sizeof(generated), "Lector");
    }
  }
  return generated;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_NEVER:
      // The page counter counts down from here and never reaches the threshold.
      return std::numeric_limits<int>::max();
  }
}

int CrossPointSettings::resolveReaderFontId(const uint8_t fontFamily, const uint8_t pointSize,
                                            const char* sdFontFamilyName) const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, pointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), pointSize);
  switch (pt) {
    case 12:
      return CHAREINK_12_FONT_ID;
    case 16:
      return CHAREINK_16_FONT_ID;
    case 14:
    default:
      return CHAREINK_14_FONT_ID;
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  return resolveReaderFontId(fontFamily, fontPointSize, sdFontFamilyName);
}

int CrossPointSettings::getReaderFontId(const ReaderPrefs& prefs) const {
  return resolveReaderFontId(prefs.fontFamily, prefs.fontPointSize, prefs.sdFontFamilyName);
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth, const uint16_t viewportHeight,
                                                      const ReaderPrefs& prefs) const {
  return makeRenderSpec(prefs, getReaderFontId(prefs), viewportWidth, viewportHeight);
}

void CrossPointSettings::applyReaderPrefs(const ReaderPrefs& p) {
  // Only the Reader Settings screen rows: the in-book toggles and the status bar block
  // are per-book and must never be pushed onto the global fields from here.
#define CP_APPLY_READER_FIELD(name) name = p.name;
  READER_LOOK_SCREEN_FIELDS(CP_APPLY_READER_FIELD)
#undef CP_APPLY_READER_FIELD
  std::memset(sdFontFamilyName, 0, sizeof(sdFontFamilyName));
  std::strncpy(sdFontFamilyName, p.sdFontFamilyName, sizeof(sdFontFamilyName) - 1);
}

StatusBarBlock CrossPointSettings::statusBar() const {
  StatusBarBlock b;
#define CP_CAPTURE_SB(prefsName, settingsName, blockName) b.blockName = settingsName;
  READER_STATUS_BAR_FIELDS(CP_CAPTURE_SB)
#undef CP_CAPTURE_SB
  b.foldTitleSource();
  return b;
}

void CrossPointSettings::setStatusBar(const StatusBarBlock& b) {
#define CP_APPLY_SB(prefsName, settingsName, blockName) settingsName = b.blockName;
  READER_STATUS_BAR_FIELDS(CP_APPLY_SB)
#undef CP_APPLY_SB
}

bool CrossPointSettings::saveToFile() const {
  // Re-assert the margin link mode before anything is written. The web settings API can
  // write screenMargin or a vertical margin straight into its field, so this is where a
  // page whose sides disagree with its mode is brought back in line. const_cast is safe:
  // the singleton is a non-const object.
  const_cast<CrossPointSettings*>(this)->normalizeMargins();

  return PersistableStore<CrossPointSettings>::saveToFile();
}

// ReaderPrefs.h is host-buildable and cannot see this class, so it carries its own copies.
static_assert(CrossPointSettings::SB_OFFBAR_OFF == StatusBarBlock::OFF_BAR_OFF, "off-bar Off value drifted");
static_assert(CrossPointSettings::IMAGES_DISPLAY_BW == reader_defaults::IMAGES_DISPLAY_BW &&
                  CrossPointSettings::IMAGE_RENDERING_COUNT == reader_defaults::IMAGE_RENDERING_COUNT,
              "image rendering values drifted");
static_assert(CrossPointSettings::PARAGRAPH_NUMBERING_COUNT == reader_defaults::PARAGRAPH_NUMBERING_COUNT,
              "paragraph numbering count drifted");
