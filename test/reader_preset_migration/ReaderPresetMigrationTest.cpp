// Host tests for the migrations a reader preset needs when it was saved before the
// Horizontal/Vertical margin split and before Embedded Style became two switches.
// The preset file stores NAMED KEYS, so a missing key is the only signal that a preset
// predates a change — these tests pin what each missing key must fall back to.
#include <gtest/gtest.h>

#include "ReaderPresetMigration.h"

namespace {

using reader_preset_migration::LegacyKeys;

// A preset saved with uniform margins on holds no vertical values of its own: both
// sides were drawn at the horizontal margin. Reading it without the migration leaves
// the constructed defaults in place, so the book's vertical margins move on update.
TEST(ReaderPresetMigration, UniformMarginsOnCopiesTheHorizontalMarginIntoBothSides) {
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
  EXPECT_EQ(1, p.verticalMarginsLinked);
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
  EXPECT_EQ(0, p.verticalMarginsLinked);
}

// A preset written after the split carries the new key and must not be touched.
TEST(ReaderPresetMigration, ANewPresetIsLeftAlone) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.verticalMarginsLinked = 0;
  p.embeddedTextStyle = 1;
  p.embeddedLayoutStyle = 0;

  LegacyKeys keys;
  keys.hasVerticalMarginsLinked = true;
  keys.hasEmbeddedLayoutStyle = true;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(12, p.screenMarginTop);
  EXPECT_EQ(18, p.screenMarginBottom);
  EXPECT_EQ(0, p.verticalMarginsLinked);
  EXPECT_EQ(1, p.embeddedTextStyle);
  EXPECT_EQ(0, p.embeddedLayoutStyle);
}

// The old single Embedded Style choice lands in the text switch, and the layout switch
// follows it: a preset saved with the style off must not hand the book's geometry back.
TEST(ReaderPresetMigration, TheLayoutSwitchFollowsTheOldSingleChoice) {
  for (const uint8_t choice : {uint8_t{0}, uint8_t{1}}) {
    ReaderPrefs p;
    p.embeddedTextStyle = choice;
    p.embeddedLayoutStyle = 1;  // the constructed default

    LegacyKeys keys;  // no embeddedLayoutStyle key: this preset predates the split
    reader_preset_migration::apply(keys, p);

    EXPECT_EQ(choice, p.embeddedTextStyle);
    EXPECT_EQ(choice, p.embeddedLayoutStyle);
  }
}

}  // namespace
