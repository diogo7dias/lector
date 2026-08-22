#pragma once

#include "activities/reader/ReaderPrefs.h"
#include "util/MarginLink.h"

// Migrations a saved reader preset needs when it predates a settings change.
//
// Presets are stored as NAMED KEYS, so "this preset is old" is only ever visible as a
// key that is not there. The rule lives here rather than inline in ReaderPresetStore so
// it can be tested without ArduinoJson, and so it cannot drift from the matching rule
// CrossPointSettings applies to the global file.
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

// `p` has already been filled from whatever named keys the preset held; every key it
// did not hold is still at its constructed default. Fix up the splits.
inline void apply(const LegacyKeys& keys, ReaderPrefs& p) {
  // Margin link modes. Three vintages, newest first: a preset with the mode key stands
  // as it is; one with only the vertical on/off key keeps how its two vertical sides
  // were edited; the oldest carried "uniform", which is what All Sides means today.
  if (!keys.hasMarginLinkMode) {
    if (keys.hasVerticalMarginsLinked) {
      p.marginLinkMode = margin_link::toStored(keys.verticalMarginsLinked ? margin_link::Mode::TopBottom
                                                                          : margin_link::Mode::Separate);
    } else if (keys.hasUniformMargins) {
      const margin_link::State migrated = margin_link::migrateFromUniform(
          keys.uniformMargins != 0, {p.screenMargin, p.screenMarginTop, p.screenMarginBottom}, p.dynamicMargins);
      p.screenMargin = migrated.margins.horizontal;
      p.screenMarginTop = migrated.margins.top;
      p.screenMarginBottom = migrated.margins.bottom;
      p.dynamicMargins = migrated.dynamicMargins;
      p.marginLinkMode = margin_link::toStored(migrated.mode);
    }
  }

  // Embedded Style split. The old key is reused for the text switch, so only the layout
  // switch needs seeding: someone who turned the style off wanted the book's own styling
  // gone, not just its fonts.
  if (!keys.hasEmbeddedLayoutStyle) p.embeddedLayoutStyle = p.embeddedTextStyle;
}

}  // namespace reader_preset_migration
