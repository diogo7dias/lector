// Host tests for the migrations a reader preset needs when it was saved before the
// margin link modes and before Embedded Style became two switches. The preset file
// stores NAMED KEYS, so a missing key is the only signal that a preset predates a
// change — these tests pin what each missing key must fall back to.
#include <gtest/gtest.h>

#include "ReaderPresetMigration.h"
#include "util/MarginLink.h"

namespace {

using margin_link::Mode;
using reader_preset_migration::LegacyKeys;

// A preset saved with uniform margins on holds no vertical values of its own: both
// sides were drawn at the horizontal margin. That is exactly All Sides, so the preset
// comes back as the same page it was saved as.
TEST(ReaderPresetMigration, UniformMarginsOnComesBackAsAllSides) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.screenMarginTop = 5;  // the constructed default, never written by this preset
  p.screenMarginBottom = 5;

  LegacyKeys keys;
  keys.hasUniformMargins = true;
  keys.uniformMargins = 1;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(35, p.screenMarginTop);
  EXPECT_EQ(35, p.screenMarginBottom);
  EXPECT_EQ(margin_link::toStored(Mode::AllSides), p.marginLinkMode);
}

// Dynamic Margins computes its own horizontal margin, which All Sides cannot allow.
TEST(ReaderPresetMigration, UniformMarginsOnForcesDynamicMarginsOff) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.dynamicMargins = 2;

  LegacyKeys keys;
  keys.hasUniformMargins = true;
  keys.uniformMargins = 1;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(0, p.dynamicMargins);
}

// With it off the two stored values were already the vertical margins, so they stand.
TEST(ReaderPresetMigration, UniformMarginsOffKeepsTheStoredVerticalMargins) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;

  LegacyKeys keys;
  keys.hasUniformMargins = true;
  keys.uniformMargins = 0;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(12, p.screenMarginTop);
  EXPECT_EQ(18, p.screenMarginBottom);
  EXPECT_EQ(margin_link::toStored(Mode::Separate), p.marginLinkMode);
}

// The middle vintage: saved after the Horizontal/Vertical split but before the modes.
// Its one on/off key is the whole record of how the two vertical sides were edited.
TEST(ReaderPresetMigration, TheOldVerticalLinkKeyBecomesTopBottomOrSeparate) {
  for (const uint8_t linked : {uint8_t{0}, uint8_t{1}}) {
    ReaderPrefs p;
    p.screenMargin = 35;
    p.screenMarginTop = 12;
    p.screenMarginBottom = 18;

    LegacyKeys keys;
    keys.hasVerticalMarginsLinked = true;
    keys.verticalMarginsLinked = linked;
    reader_preset_migration::apply(keys, p);

    EXPECT_EQ(margin_link::toStored(linked ? Mode::TopBottom : Mode::Separate), p.marginLinkMode);
    // Only the mode is being recovered here; neither side may move.
    EXPECT_EQ(12, p.screenMarginTop);
    EXPECT_EQ(18, p.screenMarginBottom);
  }
}

// A preset written with the modes and the lean keys carries both and must not be touched.
TEST(ReaderPresetMigration, ANewPresetIsLeftAlone) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.marginLinkMode = margin_link::toStored(Mode::Separate);
  p.dynamicMargins = 2;
  p.embeddedStyle = EMBEDDED_STYLE_TEXT_ONLY;
  p.paragraphIndentSpaces = 4;
  p.characterSpacing = 3;
  p.imageRendering = 2;

  LegacyKeys keys;
  keys.hasMarginLinkMode = true;
  keys.hasParagraphIndentSpaces = true;
  keys.hasCharacterSpacingPx = true;
  keys.hasEmbeddedStyleMode = true;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(12, p.screenMarginTop);
  EXPECT_EQ(18, p.screenMarginBottom);
  EXPECT_EQ(35, p.screenMargin);
  EXPECT_EQ(margin_link::toStored(Mode::Separate), p.marginLinkMode);
  EXPECT_EQ(2, p.dynamicMargins);
  EXPECT_EQ(EMBEDDED_STYLE_TEXT_ONLY, p.embeddedStyle);
  EXPECT_EQ(4, p.paragraphIndentSpaces);
  EXPECT_EQ(3, p.characterSpacing);
  EXPECT_EQ(2, p.imageRendering);
}

// Two Embedded Style switches became one 3-way mode: legacy on stays Full, either off becomes Off.
TEST(ReaderPresetMigration, TheTwoStyleSwitchesBecomeOneAndOffWins) {
  for (const uint8_t text : {uint8_t{0}, uint8_t{1}}) {
    for (const uint8_t layout : {uint8_t{0}, uint8_t{1}}) {
      ReaderPrefs p;
      p.embeddedStyle = text;
      LegacyKeys keys;
      keys.hasEmbeddedLayoutStyle = true;
      keys.embeddedLayoutStyle = layout;
      reader_preset_migration::apply(keys, p);
      EXPECT_EQ(text && layout ? EMBEDDED_STYLE_FULL : EMBEDDED_STYLE_OFF, p.embeddedStyle) << int(text) << int(layout);
    }
  }
  // No layout key: legacy 2-way "embeddedStyle" maps 0 -> Off, 1 -> Full.
  for (const uint8_t choice : {uint8_t{0}, uint8_t{1}}) {
    ReaderPrefs p;
    LegacyKeys keys;
    keys.hasLegacyEmbeddedStyle = true;
    keys.legacyEmbeddedStyle = choice;
    reader_preset_migration::apply(keys, p);
    EXPECT_EQ(choice ? EMBEDDED_STYLE_FULL : EMBEDDED_STYLE_OFF, p.embeddedStyle);
  }
}

// The indent was Book or a percent of the column; it counts spaces now.
TEST(ReaderPresetMigration, TheIndentMapsToTheNearestSpaceCount) {
  const auto migrated = [](uint8_t mode, uint8_t percent) {
    ReaderPrefs p;
    LegacyKeys keys;
    keys.hasFirstLineIndent = true;
    keys.firstLineIndentMode = mode;
    keys.firstLineIndentPercent = percent;
    reader_preset_migration::apply(keys, p);
    return p.paragraphIndentSpaces;
  };
  EXPECT_EQ(2, migrated(0, 40));  // Book mode: there is no publisher choice left, so the default
  EXPECT_EQ(0, migrated(1, 0));   // flush stays flush
  EXPECT_EQ(1, migrated(1, 5));   // anything above zero keeps one space
  EXPECT_EQ(2, migrated(1, 20));  // both old defaults land on the new default
  EXPECT_EQ(2, migrated(1, 23));
  EXPECT_EQ(3, migrated(1, 25));
  EXPECT_EQ(5, migrated(1, 100));  // and the top of the old range on the top of the new
}

// Character Spacing moved from half pixels under "characterSpacing" to whole pixels under
// "characterSpacingPx": the old key converts, the new one is never converted again.
TEST(ReaderPresetMigration, CharacterSpacingMovesToWholePixels) {
  const auto migrated = [](uint8_t stored) {
    ReaderPrefs p;
    LegacyKeys keys;
    keys.hasLegacyCharacterSpacing = true;
    keys.legacyCharacterSpacing = stored;
    reader_preset_migration::apply(keys, p);
    return p.characterSpacing;
  };
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING, migrated(5));      // as designed stays as designed
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING + 1, migrated(7));  // +1 px (two half pixels)
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING - 1, migrated(3));
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING + 1, migrated(6));  // half a pixel rounds away from zero
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING - 1, migrated(4));
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING + 2, migrated(10));  // +2.5 clamps to +2
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING - 2, migrated(0));
}

// Placeholder and Suppress were inert from 2026-08-11 and the row is back, so an old
// preset must not start hiding images. The side margin and word spacing land on their grids.
TEST(ReaderPresetMigration, AnOldPresetGetsDisplayImagesAndTheNewGrids) {
  ReaderPrefs p;
  p.imageRendering = 2;
  p.wordSpacing = 107;
  p.screenMargin = 60;
  p.screenMarginTop = 60;
  p.screenMarginBottom = 60;
  p.marginLinkMode = margin_link::toStored(Mode::AllSides);
  LegacyKeys keys;
  keys.hasMarginLinkMode = true;
  reader_preset_migration::apply(keys, p);
  EXPECT_EQ(0, p.imageRendering);
  EXPECT_EQ(100, p.wordSpacing);
  EXPECT_EQ(40, p.screenMargin);
  EXPECT_EQ(40, p.screenMarginTop);  // All Sides carries the clamped side margin onto every side
  EXPECT_EQ(40, p.screenMarginBottom);
}

// CrossPointSettings resaves only when the margin rule changed something; the return
// value is its only signal.
TEST(ReaderPresetMigration, MarginLinkReportsWhetherItMigrated) {
  uint8_t h = 20, top = 12, bottom = 18, dynamic = 0, mode = 0;
  LegacyKeys current;
  current.hasMarginLinkMode = true;
  current.hasVerticalMarginsLinked = true;
  EXPECT_FALSE(reader_preset_migration::migrateMarginLink(current, h, top, bottom, dynamic, mode));
  EXPECT_EQ(0, mode);

  LegacyKeys none;
  EXPECT_FALSE(reader_preset_migration::migrateMarginLink(none, h, top, bottom, dynamic, mode));

  LegacyKeys vertical;
  vertical.hasVerticalMarginsLinked = true;
  vertical.verticalMarginsLinked = 0;
  EXPECT_TRUE(reader_preset_migration::migrateMarginLink(vertical, h, top, bottom, dynamic, mode));
  EXPECT_EQ(margin_link::toStored(Mode::Separate), mode);

  LegacyKeys uniform;
  uniform.hasUniformMargins = true;
  EXPECT_TRUE(reader_preset_migration::migrateMarginLink(uniform, h, top, bottom, dynamic, mode));
  EXPECT_EQ(margin_link::toStored(Mode::AllSides), mode);
  EXPECT_EQ(20, top);
  EXPECT_EQ(20, bottom);
}

// A theme saved with the status bar carries its own bar; applying it puts that bar on
// the book, and nothing else of the book moves.
TEST(ReaderPresetMigration, ThemeWithStatusBarAppliesItsOwnBar) {
  ReaderPrefs current;
  current.sbClockPos = 3;
  current.paragraphNumberSize = 1;

  ReaderPrefs saved;
  saved.sbClockPos = 9;
  saved.sbTitlePos = 2;
  saved.statusBarEnabled = 0;
  saved.paragraphNumberSize = 2;

  reader_preset_migration::keepStatusBarWhenAbsent(true, current, saved);

  EXPECT_EQ(9, saved.sbClockPos);
  EXPECT_EQ(2, saved.sbTitlePos);
  EXPECT_EQ(0, saved.statusBarEnabled);
  EXPECT_EQ(2, saved.paragraphNumberSize);
}

// A theme saved before it carried the bar keeps the bar the book has now, never zeroes it.
TEST(ReaderPresetMigration, ThemeWithoutStatusBarKeepsTheBookBar) {
  ReaderPrefs current;
  current.sbClockPos = 3;
  current.statusBarEnabled = 0;

  ReaderPrefs old;  // constructed defaults: what a key-less preset loads as
  reader_preset_migration::keepStatusBarWhenAbsent(false, current, old);

  EXPECT_EQ(3, old.sbClockPos);
  EXPECT_EQ(0, old.statusBarEnabled);
}

}  // namespace
