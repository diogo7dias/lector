#pragma once

#include "activities/reader/ReaderPrefs.h"
#include "util/MarginLink.h"

// Migrations a saved reader preset needs when it predates a settings change.
//
// Presets are stored as NAMED KEYS, so "this preset is old" is only ever visible as a
// key that is not there. The rule lives here rather than inline in ReaderPresetStore so
// it can be tested without ArduinoJson, and CrossPointSettings runs the margin half of
// it (migrateMarginLink) on the global file, so the two cannot drift.
namespace reader_preset_migration {

// Which of the keys that mark a preset's vintage were actually present in the file.
struct LegacyKeys {
  bool hasUniformMargins = false;         // oldest: "all sides use the horizontal margin"
  uint8_t uniformMargins = 1;             // its value, when present
  bool hasVerticalMarginsLinked = false;  // middle: the vertical sides' on/off link
  uint8_t verticalMarginsLinked = 1;      // its value, when present
  bool hasMarginLinkMode = false;         // current: the three link modes
  bool hasEmbeddedLayoutStyle = false;    // absent = one Embedded Style choice, not two
};

// Reads those keys from any ArduinoJson object or document; templated so this header
// needs no ArduinoJson itself.
template <typename Json>
LegacyKeys readLegacyKeys(const Json& obj) {
  LegacyKeys keys;
  keys.hasUniformMargins = obj["uniformMargins"].template is<uint8_t>();
  if (keys.hasUniformMargins) keys.uniformMargins = obj["uniformMargins"].template as<uint8_t>();
  keys.hasVerticalMarginsLinked = obj["verticalMarginsLinked"].template is<uint8_t>();
  if (keys.hasVerticalMarginsLinked) keys.verticalMarginsLinked = obj["verticalMarginsLinked"].template as<uint8_t>();
  keys.hasMarginLinkMode = obj["marginLinkMode"].template is<uint8_t>();
  keys.hasEmbeddedLayoutStyle = obj["embeddedLayoutStyle"].template is<uint8_t>();
  return keys;
}

// Margin link modes. Three vintages, newest first: a file with the mode key stands as
// it is; one with only the vertical on/off key keeps how its two vertical sides were
// edited; the oldest carried "uniform", which is what All Sides means today (such a file
// has no vertical values of its own, so both sides take the horizontal margin, which is
// what the reader was already drawing). Over bare fields so a preset and the global
// settings run the same rule. Returns true when it migrated, i.e. the file wants a resave.
inline bool migrateMarginLink(const LegacyKeys& keys, uint8_t& screenMargin, uint8_t& screenMarginTop,
                              uint8_t& screenMarginBottom, uint8_t& dynamicMargins, uint8_t& marginLinkMode) {
  if (keys.hasMarginLinkMode) return false;
  if (keys.hasVerticalMarginsLinked) {
    marginLinkMode =
        margin_link::toStored(keys.verticalMarginsLinked ? margin_link::Mode::TopBottom : margin_link::Mode::Separate);
    return true;
  }
  if (!keys.hasUniformMargins) return false;
  const margin_link::State migrated = margin_link::migrateFromUniform(
      keys.uniformMargins != 0, {screenMargin, screenMarginTop, screenMarginBottom}, dynamicMargins);
  screenMargin = migrated.margins.horizontal;
  screenMarginTop = migrated.margins.top;
  screenMarginBottom = migrated.margins.bottom;
  dynamicMargins = migrated.dynamicMargins;
  marginLinkMode = margin_link::toStored(migrated.mode);
  return true;
}

// `p` has already been filled from whatever named keys the preset held; every key it
// did not hold is still at its constructed default. Fix up the splits.
inline void apply(const LegacyKeys& keys, ReaderPrefs& p) {
  migrateMarginLink(keys, p.screenMargin, p.screenMarginTop, p.screenMarginBottom, p.dynamicMargins, p.marginLinkMode);

  // Embedded Style split. The old key is reused for the text switch, so only the layout
  // switch needs seeding: someone who turned the style off wanted the book's own styling
  // gone, not just its fonts.
  if (!keys.hasEmbeddedLayoutStyle) p.embeddedLayoutStyle = p.embeddedTextStyle;
}

}  // namespace reader_preset_migration
