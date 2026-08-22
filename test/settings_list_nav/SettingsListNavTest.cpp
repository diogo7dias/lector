#include <gtest/gtest.h>

#include <vector>

#include "activities/settings/SettingsListNav.h"

using settings_nav::firstLandableRow;
using settings_nav::nextRow;
using settings_nav::nextSection;

namespace {

// H = heading, . = a real row. Mirrors the flat settings list: every section
// opens with a heading, so index 0 is never landable.
// 0:H 1:. 2:. 3:H 4:. 5:H 6:. 7:.
const std::vector<bool> kList = {true, false, false, true, false, true, false, false};

TEST(SettingsListNav, FirstLandableRowSkipsTheOpeningHeading) {
  EXPECT_EQ(firstLandableRow(kList), 1);
  EXPECT_EQ(firstLandableRow({false, true}), 0);
}

TEST(SettingsListNav, FirstLandableRowIsZeroWhenNothingIsLandable) {
  EXPECT_EQ(firstLandableRow({}), 0);
  EXPECT_EQ(firstLandableRow({true, true}), 0);
}

TEST(SettingsListNav, SteppingForwardSkipsHeadings) {
  EXPECT_EQ(nextRow(1, kList, true), 2);
  EXPECT_EQ(nextRow(2, kList, true), 4);  // steps over the heading at 3
  EXPECT_EQ(nextRow(4, kList, true), 6);  // steps over the heading at 5
}

TEST(SettingsListNav, SteppingForwardWrapsToTheFirstRow) {
  EXPECT_EQ(nextRow(7, kList, true), 1);  // past the end, over the heading at 0
}

TEST(SettingsListNav, SteppingBackwardSkipsHeadings) {
  EXPECT_EQ(nextRow(6, kList, false), 4);
  EXPECT_EQ(nextRow(4, kList, false), 2);
}

TEST(SettingsListNav, SteppingBackwardWrapsToTheLastRow) { EXPECT_EQ(nextRow(1, kList, false), 7); }

TEST(SettingsListNav, SteppingHoldsStillWhenNoRowIsLandable) {
  const std::vector<bool> headingsOnly = {true, true, true};
  EXPECT_EQ(nextRow(0, headingsOnly, true), 0);
  EXPECT_EQ(nextRow(0, headingsOnly, false), 0);
  EXPECT_EQ(nextRow(0, {}, true), 0);
}

TEST(SettingsListNav, SectionForwardLandsOnTheFirstRowUnderTheNextHeading) {
  EXPECT_EQ(nextSection(1, kList, true), 4);
  EXPECT_EQ(nextSection(2, kList, true), 4);
  EXPECT_EQ(nextSection(4, kList, true), 6);
}

TEST(SettingsListNav, SectionForwardWrapsToTheFirstSection) {
  EXPECT_EQ(nextSection(6, kList, true), 1);
  EXPECT_EQ(nextSection(7, kList, true), 1);
}

TEST(SettingsListNav, SectionBackwardSnapsToTheStartOfTheCurrentSection) {
  // From mid-section the first press goes to the section's own first row, so a
  // long list can be walked back without overshooting the section just entered.
  EXPECT_EQ(nextSection(2, kList, false), 1);
  EXPECT_EQ(nextSection(7, kList, false), 6);
}

TEST(SettingsListNav, SectionBackwardFromASectionStartGoesToThePreviousSection) {
  EXPECT_EQ(nextSection(6, kList, false), 4);
  EXPECT_EQ(nextSection(4, kList, false), 1);
}

TEST(SettingsListNav, SectionBackwardWrapsToTheLastSection) { EXPECT_EQ(nextSection(1, kList, false), 6); }

TEST(SettingsListNav, SectionJumpsFallBackToPlainStepsWithoutHeadings) {
  const std::vector<bool> flat = {false, false, false};
  EXPECT_EQ(nextSection(0, flat, true), 0);
  EXPECT_EQ(nextSection(1, flat, false), 0);
}

TEST(SettingsListNav, SectionJumpsHoldStillWhenNoRowIsLandable) {
  EXPECT_EQ(nextSection(0, {true, true}, true), 0);
  EXPECT_EQ(nextSection(0, {}, false), 0);
}

}  // namespace
