#include "CrossPointSettings.h"

#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "I18nKeys.h"
#include "SettingsList.h"
#include "fontIds.h"

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

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

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
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  doc["fontFamily"] = fontFamily;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Paperback Look — not in SettingsList (in-book menu only), persist manually.
  doc["paperbackLookBody"] = paperbackLookBody;
  doc["paperbackLookStatus"] = paperbackLookStatus;
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        continue;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        const std::string decoded = obfuscation::deobfuscateFromBase64(doc[obfKey] | "", &ok);
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
        v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
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
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = VOLLKORN;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));

  // Paperback Look — not in SettingsList (in-book menu only). Absent key => default
  // ON (1); clamp any stray value to a 0/1 boolean.
  paperbackLookBody = clamp(doc["paperbackLookBody"] | (uint8_t)1, (uint8_t)2, (uint8_t)1);
  paperbackLookStatus = clamp(doc["paperbackLookStatus"] | (uint8_t)1, (uint8_t)2, (uint8_t)1);

  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.clockUtcOffsetQ = clockUtcOffsetQ;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphSpacing = paragraphSpacing;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  spec.firstLineIndent = firstLineIndent;
  return spec;
}

float CrossPointSettings::resolveLineCompression(const uint8_t lineSpacingPercent) {
  uint8_t pct = lineSpacingPercent;
  if (pct < MIN_LINE_SPACING_PERCENT) pct = MIN_LINE_SPACING_PERCENT;
  if (pct > MAX_LINE_SPACING_PERCENT) pct = MAX_LINE_SPACING_PERCENT;
  return static_cast<float>(pct) / 100.0f;
}

float CrossPointSettings::getReaderLineCompression() const { return resolveLineCompression(lineSpacingPercent); }

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
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
  }
}

int CrossPointSettings::resolveReaderFontId(const uint8_t fontFamily, const uint8_t fontSize,
                                            const char* sdFontFamilyName) const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  switch (fontFamily) {
    case VOLLKORN:
    default:
      switch (fontSize) {
        case SMALL:
          return VOLLKORN_12_FONT_ID;
        case MEDIUM:
        default:
          return VOLLKORN_14_FONT_ID;
        case LARGE:
          return VOLLKORN_16_FONT_ID;
        case EXTRA_LARGE:
          return VOLLKORN_18_FONT_ID;
      }
  }
}

int CrossPointSettings::getReaderFontId() const { return resolveReaderFontId(fontFamily, fontSize, sdFontFamilyName); }

int CrossPointSettings::getReaderFontId(const ReaderPrefs& prefs) const {
  return resolveReaderFontId(prefs.fontFamily, prefs.fontSize, prefs.sdFontFamilyName);
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth, const uint16_t viewportHeight,
                                                      const ReaderPrefs& prefs) const {
  ReaderRenderSpec spec;
  spec.fontId = resolveReaderFontId(prefs.fontFamily, prefs.fontSize, prefs.sdFontFamilyName);
  spec.lineCompression = resolveLineCompression(prefs.lineSpacingPercent);
  spec.extraParagraphSpacing = prefs.extraParagraphSpacing != 0;
  spec.paragraphSpacing = prefs.paragraphSpacing;
  spec.paragraphAlignment = prefs.paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = prefs.hyphenationEnabled != 0;
  spec.embeddedStyle = prefs.embeddedStyle != 0;
  spec.imageRendering = prefs.imageRendering;
  spec.focusReadingEnabled = prefs.focusReadingEnabled != 0;
  spec.firstLineIndent = prefs.firstLineIndent;
  return spec;
}

void CrossPointSettings::applyReaderPrefs(const ReaderPrefs& p) {
  fontFamily = p.fontFamily;
  fontSize = p.fontSize;
  lineSpacingPercent = p.lineSpacingPercent;
  paragraphAlignment = p.paragraphAlignment;
  extraParagraphSpacing = p.extraParagraphSpacing;
  paragraphSpacing = p.paragraphSpacing;
  screenMargin = p.screenMargin;
  screenMarginTop = p.screenMarginTop;
  screenMarginBottom = p.screenMarginBottom;
  uniformMargins = p.uniformMargins;
  dynamicMargins = p.dynamicMargins;
  firstLineIndent = p.firstLineIndent;
  focusReadingEnabled = p.focusReadingEnabled;
  guideDotsEnabled = p.guideDotsEnabled;
  hyphenationEnabled = p.hyphenationEnabled;
  embeddedStyle = p.embeddedStyle;
  textAntiAliasing = p.textAntiAliasing;
  imageRendering = p.imageRendering;
  std::memset(sdFontFamilyName, 0, sizeof(sdFontFamilyName));
  std::strncpy(sdFontFamilyName, p.sdFontFamilyName, sizeof(sdFontFamilyName) - 1);
}

void CrossPointSettings::beginReaderEditOverlay(const ReaderPrefs& startValues) {
  if (!readerEditOverlayActive_) {
    readerEditBackup_ = ReaderPrefs::fromGlobal();  // the true global reader values
    readerEditOverlayActive_ = true;
  }
  applyReaderPrefs(startValues);  // editor starts from the book's current values
}

ReaderPrefs CrossPointSettings::endReaderEditOverlay() {
  const ReaderPrefs edited = ReaderPrefs::fromGlobal();  // whatever the editor left on the live fields
  if (readerEditOverlayActive_) {
    applyReaderPrefs(readerEditBackup_);  // restore the true global values
    readerEditOverlayActive_ = false;
  }
  return edited;
}

bool CrossPointSettings::saveToFile() const {
  if (!readerEditOverlayActive_) {
    return PersistableStore<CrossPointSettings>::saveToFile();
  }
  // A per-book reader edit is overlaid on the live reader fields. Persist the
  // GLOBAL values (the backup) so settings.json never captures a book's per-book
  // settings, even if a background task saves mid-edit. const_cast is safe: the
  // singleton is a non-const object; this only transiently swaps its reader fields
  // out for the write, then restores the overlay.
  auto* self = const_cast<CrossPointSettings*>(this);
  const ReaderPrefs overlaid = ReaderPrefs::fromGlobal();
  self->applyReaderPrefs(readerEditBackup_);
  const bool ok = PersistableStore<CrossPointSettings>::saveToFile();
  self->applyReaderPrefs(overlaid);
  return ok;
}
