#pragma once

#include "ReaderPrefs.h"
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
  bool hasEmbeddedLayoutStyle = false;    // present = the two-switch Embedded Style vintage
  uint8_t embeddedLayoutStyle = 1;        // its value, when present
  // Lean typography (v27): the indent counts spaces, character spacing counts whole pixels,
  // and the style switch is one. A file without the indent key is older than that.
  bool hasParagraphIndentSpaces = false;
  bool hasFirstLineIndent = false;  // the old "Book or percent" pair
  uint8_t firstLineIndentMode = 1;
  uint8_t firstLineIndentPercent = 0;
  bool hasCharacterSpacingPx = false;
  bool hasLegacyCharacterSpacing = false;  // half pixels, offset 5
  uint8_t legacyCharacterSpacing = 5;
  bool hasEmbeddedStyleMode = false;    // 3-way: 0=Off, 1=Text Only, 2=Full
  bool hasLegacyEmbeddedStyle = false;  // legacy 2-way "embeddedStyle" (0=Off, 1=Full)
  uint8_t legacyEmbeddedStyle = 1;
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
  if (keys.hasEmbeddedLayoutStyle) keys.embeddedLayoutStyle = obj["embeddedLayoutStyle"].template as<uint8_t>();
  keys.hasParagraphIndentSpaces = obj["paragraphIndentSpaces"].template is<uint8_t>();
  keys.hasFirstLineIndent =
      obj["firstLineIndentMode"].template is<uint8_t>() && obj["firstLineIndentPercent"].template is<uint8_t>();
  if (keys.hasFirstLineIndent) {
    keys.firstLineIndentMode = obj["firstLineIndentMode"].template as<uint8_t>();
    keys.firstLineIndentPercent = obj["firstLineIndentPercent"].template as<uint8_t>();
  }
  keys.hasCharacterSpacingPx = obj["characterSpacingPx"].template is<uint8_t>();
  keys.hasLegacyCharacterSpacing = obj["characterSpacing"].template is<uint8_t>();
  if (keys.hasLegacyCharacterSpacing) keys.legacyCharacterSpacing = obj["characterSpacing"].template as<uint8_t>();
  keys.hasEmbeddedStyleMode = obj["embeddedStyleMode"].template is<uint8_t>();
  keys.hasLegacyEmbeddedStyle = obj["embeddedStyle"].template is<uint8_t>();
  if (keys.hasLegacyEmbeddedStyle) keys.legacyEmbeddedStyle = obj["embeddedStyle"].template as<uint8_t>();
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

// Lean typography, over bare fields so a preset and the global settings file run one rule.
// A file with the "paragraphIndentSpaces" key was written by a lean build and stands as it is.
// An older one carries the indent as Book or a percent of the column, character spacing in
// half pixels under the old key, and the style as two switches; a key it does not hold
// leaves its default.
inline void migrateLeanTypography(const LegacyKeys& keys, uint8_t& paragraphIndentSpaces, uint8_t& embeddedStyle,
                                  uint8_t& characterSpacing) {
  if (!keys.hasParagraphIndentSpaces) {
    if (keys.hasFirstLineIndent) {
      paragraphIndentSpaces =
          reader_defaults::indentSpacesFromLegacy(keys.firstLineIndentMode, keys.firstLineIndentPercent);
    }
    if (!keys.hasCharacterSpacingPx && keys.hasLegacyCharacterSpacing) {
      characterSpacing = reader_defaults::characterSpacingFromLegacy(keys.legacyCharacterSpacing);
    }
  }
  // 3-way Embedded Style (Off=0 / Text Only=1 / Full=2): files written under the old
  // 2-way "embeddedStyle" key (0=Off, 1=Full) or two-switch split map cleanly without
  // silently downgrading Full to Text Only.
  if (!keys.hasEmbeddedStyleMode) {
    if (keys.hasEmbeddedLayoutStyle) {
      const uint8_t textVal = keys.hasLegacyEmbeddedStyle ? keys.legacyEmbeddedStyle : embeddedStyle;
      embeddedStyle = (textVal != 0 && keys.embeddedLayoutStyle != 0) ? EMBEDDED_STYLE_FULL : EMBEDDED_STYLE_OFF;
    } else if (keys.hasLegacyEmbeddedStyle) {
      embeddedStyle = keys.legacyEmbeddedStyle != 0 ? EMBEDDED_STYLE_FULL : EMBEDDED_STYLE_OFF;
    } else if (!keys.hasParagraphIndentSpaces && embeddedStyle == 1) {
      embeddedStyle = EMBEDDED_STYLE_FULL;
    }
  }
}

// A preset saved before it carried the status bar has no bar of its own. It takes the bar
// this book has now, the same way a missing key takes the default: the bar is never zeroed.
inline void keepStatusBarWhenAbsent(const bool presetHasStatusBar, const ReaderPrefs& current, ReaderPrefs& incoming) {
  if (!presetHasStatusBar) setStatusBarOf(incoming, statusBarOf(current));
}

// `p` has already been filled from whatever named keys the preset held; every key it
// did not hold is still at its constructed default. Fix up the splits.
inline void apply(const LegacyKeys& keys, ReaderPrefs& p) {
  migrateMarginLink(keys, p.screenMargin, p.screenMarginTop, p.screenMarginBottom, p.dynamicMargins, p.marginLinkMode);
  migrateLeanTypography(keys, p.paragraphIndentSpaces, p.embeddedStyle, p.characterSpacing);
  if (!keys.hasParagraphIndentSpaces) settleLegacyLookValues(p);
}

}  // namespace reader_preset_migration
