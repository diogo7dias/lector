// Host tests for the OTA version comparison. Every version string this firmware sees
// starts with a name ("lector 0.24.1", the tag "lector-0.24.2", upstream's "v1.5.0"),
// which is what the previous sscanf("%d.%d.%d") comparison could not read at all.
#include <gtest/gtest.h>

#include "FirmwareVersion.h"

namespace {

TEST(FirmwareVersionParse, ReadsTheNumbersAfterAName) {
  firmware_version::Semver v;
  ASSERT_TRUE(firmware_version::parse("lector 0.24.1", &v));
  EXPECT_EQ(v.major, 0);
  EXPECT_EQ(v.minor, 24);
  EXPECT_EQ(v.patch, 1);
}

TEST(FirmwareVersionParse, ReadsATagAndAnUpstreamTag) {
  firmware_version::Semver tag;
  ASSERT_TRUE(firmware_version::parse("lector-1.2.3", &tag));
  EXPECT_EQ(tag.minor, 2);

  firmware_version::Semver upstream;
  ASSERT_TRUE(firmware_version::parse("v1.5.0", &upstream));
  EXPECT_EQ(upstream.major, 1);
  EXPECT_EQ(upstream.patch, 0);
}

TEST(FirmwareVersionParse, KeepsTheWholeMinorField) {
  // Stepping into the middle of the run would read "24.1" and then miss a third field.
  firmware_version::Semver v;
  ASSERT_TRUE(firmware_version::parse("lector 0.24.1-rc+ab12cd", &v));
  EXPECT_EQ(v.minor, 24);
  EXPECT_EQ(v.patch, 1);
}

TEST(FirmwareVersionParse, RejectsAVersionWithNoThreeNumberRun) {
  firmware_version::Semver v;
  EXPECT_FALSE(firmware_version::parse("lector.exp.7", &v));
  EXPECT_FALSE(firmware_version::parse("lector", &v));
  EXPECT_FALSE(firmware_version::parse("", &v));
  EXPECT_FALSE(firmware_version::parse(nullptr, &v));
}

TEST(FirmwareVersionIsNewer, ComparesEachFieldInTurn) {
  EXPECT_TRUE(firmware_version::isNewer("lector-0.24.2", "lector 0.24.1"));
  EXPECT_TRUE(firmware_version::isNewer("lector-0.25.0", "lector 0.24.9"));
  EXPECT_TRUE(firmware_version::isNewer("lector-1.0.0", "lector 0.99.99"));
  EXPECT_FALSE(firmware_version::isNewer("lector-0.24.0", "lector 0.24.1"));
  EXPECT_FALSE(firmware_version::isNewer("lector-0.24.1", "lector 0.24.1"));
}

TEST(FirmwareVersionIsNewer, TreatsAReleaseCandidateAsOlderThanItsRelease) {
  EXPECT_TRUE(firmware_version::isNewer("lector-0.24.1", "lector 0.24.1-rc+ab12cd"));
  EXPECT_FALSE(firmware_version::isNewer("lector-0.24.1", "lector 0.24.1"));
}

TEST(FirmwareVersionIsNewer, SaysNoWhenEitherSideCannotBeRead) {
  // An experimental build carries no three-part number. Offering an install against a
  // version the device cannot even name would be a guess.
  EXPECT_FALSE(firmware_version::isNewer("lector-0.24.2", "lector.exp.7"));
  EXPECT_FALSE(firmware_version::isNewer("lector.exp.8", "lector 0.24.1"));
}

}  // namespace
