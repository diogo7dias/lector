#include "ReaderPresetStore.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "ReaderPresetMigration.h"
#include "ReaderPresetNames.h"

namespace {

// Presets are written as NAMED KEYS, never as a ReaderPrefs blob. readReaderPrefs
// rejects a mismatched struct version outright, so a blob would silently wipe every
// saved preset the first time a field is added to ReaderPrefs. With named keys a
// version bump costs at most the new field falling back to its default.
//
// One table drives both directions, so a field can never be written and not read.
struct PresetField {
  const char* key;
  uint8_t ReaderPrefs::* member;
};

constexpr PresetField FIELDS[] = {
    {"fontFamily", &ReaderPrefs::fontFamily},
    {"fontSize", &ReaderPrefs::fontPointSize},
    {"lineSpacingPercent", &ReaderPrefs::lineSpacingPercent},
    {"paragraphAlignment", &ReaderPrefs::paragraphAlignment},
    {"extraParagraphSpacing", &ReaderPrefs::extraParagraphSpacing},
    {"paragraphSpacing", &ReaderPrefs::paragraphSpacing},
    {"screenMargin", &ReaderPrefs::screenMargin},
    {"screenMarginTop", &ReaderPrefs::screenMarginTop},
    {"screenMarginBottom", &ReaderPrefs::screenMarginBottom},
    {"marginLinkMode", &ReaderPrefs::marginLinkMode},
    {"dynamicMargins", &ReaderPrefs::dynamicMargins},
    {"focusReadingEnabled", &ReaderPrefs::focusReadingEnabled},
    {"guideDotsEnabled", &ReaderPrefs::guideDotsEnabled},
    {"guideDotsHidden", &ReaderPrefs::guideDotsHidden},
    {"hyphenationEnabled", &ReaderPrefs::hyphenationEnabled},
    {"embeddedStyle", &ReaderPrefs::embeddedTextStyle},
    {"embeddedLayoutStyle", &ReaderPrefs::embeddedLayoutStyle},
    {"textAntiAliasing", &ReaderPrefs::textAntiAliasing},
    {"imageRendering", &ReaderPrefs::imageRendering},
    {"paragraphNumbering", &ReaderPrefs::paragraphNumbering},
    {"paragraphNumberSize", &ReaderPrefs::paragraphNumberSize},
    {"paperbackLookBody", &ReaderPrefs::paperbackLookBody},
    {"paperbackLookStatus", &ReaderPrefs::paperbackLookStatus},
    {"firstLineIndentMode", &ReaderPrefs::firstLineIndentMode},
    {"firstLineIndentPercent", &ReaderPrefs::firstLineIndentPercent},
};

void writePrefs(JsonObject obj, const ReaderPrefs& p) {
  for (const PresetField& f : FIELDS) obj[f.key] = p.*(f.member);
  obj["sdFontFamilyName"] = p.sdFontFamilyName;
}

ReaderPrefs readPrefs(JsonObjectConst obj) {
  ReaderPrefs p;  // member initializers are the fallback for any key not present
  for (const PresetField& f : FIELDS) {
    if (obj[f.key].is<uint8_t>()) p.*(f.member) = obj[f.key].as<uint8_t>();
  }
  // Presets saved before the point-size switch hold the old 0..3 slot.
  p.fontPointSize = foldLegacyReaderFontSize(p.fontPointSize);
  // A preset written before the margin or Embedded Style splits carries the old keys;
  // which keys are missing is the only record of its vintage, so read that first.
  reader_preset_migration::LegacyKeys legacy;
  legacy.hasUniformMargins = obj["uniformMargins"].is<uint8_t>();
  if (legacy.hasUniformMargins) legacy.uniformMargins = obj["uniformMargins"].as<uint8_t>();
  legacy.hasVerticalMarginsLinked = obj["verticalMarginsLinked"].is<uint8_t>();
  if (legacy.hasVerticalMarginsLinked) legacy.verticalMarginsLinked = obj["verticalMarginsLinked"].as<uint8_t>();
  legacy.hasMarginLinkMode = obj["marginLinkMode"].is<uint8_t>();
  legacy.hasEmbeddedLayoutStyle = obj["embeddedLayoutStyle"].is<uint8_t>();
  reader_preset_migration::apply(legacy, p);
  const char* sdName = obj["sdFontFamilyName"] | "";
  // strncpy into the fixed field, then zero the tail: ReaderPrefs is compared whole
  // with memcmp, so trailing bytes must be canonical or an identical preset reads as
  // different.
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, sdName, sizeof(p.sdFontFamilyName) - 1);
  return p;
}

}  // namespace

void ReaderPresetStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["presets"].to<JsonArray>();
  for (const ReaderPreset& preset : presets) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = preset.name;
    writePrefs(obj, preset.prefs);
  }
}

bool ReaderPresetStore::fromJson(JsonVariantConst doc) {
  presets.clear();
  JsonArrayConst arr = doc["presets"].as<JsonArrayConst>();
  presets.reserve(std::min(arr.size(), MAX_PRESETS));

  for (JsonObjectConst obj : arr) {
    if (presets.size() >= MAX_PRESETS) break;
    ReaderPreset preset;
    preset.name = readerpreset::sanitizeName(obj["name"] | "");
    if (preset.name.empty()) continue;  // a preset with no name cannot be chosen
    preset.prefs = readPrefs(obj);
    presets.push_back(std::move(preset));
  }

  LOG_DBG("PRE", "Loaded %zu reader presets", presets.size());
  return true;
}

const ReaderPreset* ReaderPresetStore::get(const size_t index) const {
  return index < presets.size() ? &presets[index] : nullptr;
}

std::vector<std::string> ReaderPresetStore::names() const {
  std::vector<std::string> out;
  out.reserve(presets.size());
  for (const ReaderPreset& preset : presets) out.push_back(preset.name);
  return out;
}

bool ReaderPresetStore::add(const std::string& name, const ReaderPrefs& prefs) {
  if (isFull()) return false;
  const std::string unique = readerpreset::makeUniqueName(name, names());
  if (unique.empty()) return false;
  presets.push_back(ReaderPreset{unique, prefs});
  LOG_INF("PRE", "Saved reader preset: %s", unique.c_str());
  return saveToFile();
}

bool ReaderPresetStore::rename(const size_t index, const std::string& name) {
  if (index >= presets.size()) return false;
  const std::string unique = readerpreset::makeUniqueName(name, names(), static_cast<int>(index));
  if (unique.empty()) return false;
  presets[index].name = unique;
  return saveToFile();
}

bool ReaderPresetStore::overwrite(const size_t index, const ReaderPrefs& prefs) {
  if (index >= presets.size()) return false;
  presets[index].prefs = prefs;
  return saveToFile();
}

bool ReaderPresetStore::remove(const size_t index) {
  if (index >= presets.size()) return false;
  LOG_INF("PRE", "Removed reader preset: %s", presets[index].name.c_str());
  presets.erase(presets.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}
