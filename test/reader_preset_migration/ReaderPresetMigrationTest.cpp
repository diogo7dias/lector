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

// A preset written with the modes carries the new key and must not be touched.
TEST(ReaderPresetMigration, ANewPresetIsLeftAlone) {
  ReaderPrefs p;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.marginLinkMode = margin_link::toStored(Mode::Separate);
  p.dynamicMargins = 2;
  p.embeddedTextStyle = 1;
  p.embeddedLayoutStyle = 0;

  LegacyKeys keys;
  keys.hasMarginLinkMode = true;
  keys.hasEmbeddedLayoutStyle = true;
  reader_preset_migration::apply(keys, p);

  EXPECT_EQ(12, p.screenMarginTop);
  EXPECT_EQ(18, p.screenMarginBottom);
  EXPECT_EQ(margin_link::toStored(Mode::Separate), p.marginLinkMode);
  EXPECT_EQ(2, p.dynamicMargins);
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
