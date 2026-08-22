#pragma once

#include "activities/reader/ReaderPrefs.h"
#include "util/VerticalMargins.h"

// Migrations a saved reader preset needs when it predates a settings change.
//
// Presets are stored as NAMED KEYS, so "this preset is old" is only ever visible as a
// key that is not there. The rule lives here rather than inline in ReaderPresetStore so
// it can be tested without ArduinoJson, and so it cannot drift from the matching rule
// CrossPointSettings applies to the global file.
namespace reader_preset_migration {

// Which of the keys that mark a preset's vintage were actually present in the file.
struct LegacyKeys {
  bool hasUniformMargins = false;         // pre-split "all sides use the horizontal margin"
  uint8_t uniformMargins = 1;             // its value, when present
  bool hasVerticalMarginsLinked = false;  // the key that replaced it
  bool hasEmbeddedLayoutStyle = false;    // absent = one Embedded Style choice, not two
};

// `p` has already been filled from whatever named keys the preset held; every key it
// did not hold is still at its constructed default. Fix up the two splits.
inline void apply(const LegacyKeys& keys, ReaderPrefs& p) {
  // Horizontal/Vertical margin split. With uniform on, the preset holds no vertical
  // values of its own and both sides take the horizontal margin, which is what the
  // reader was already drawing for it. With it off, the two stored values were already
  // the vertical margins and stand as they are.
  if (keys.hasUniformMargins && !keys.hasVerticalMarginsLinked) {
    const bool uniform = keys.uniformMargins != 0;
    const auto migrated =
        vertical_margins::migrateFromUniform(uniform, p.screenMargin, {p.screenMarginTop, p.screenMarginBottom});
    p.screenMarginTop = migrated.top;
    p.screenMarginBottom = migrated.bottom;
    p.verticalMarginsLinked = uniform ? 1 : 0;
  }

  // Embedded Style split. The old key is reused for the text switch, so only the layout
  // switch needs seeding: someone who turned the style off wanted the book's own styling
  // gone, not just its fonts.
  if (!keys.hasEmbeddedLayoutStyle) p.embeddedLayoutStyle = p.embeddedTextStyle;
}

}  // namespace reader_preset_migration
