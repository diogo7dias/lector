#include <gtest/gtest.h>

#include "src/activities/ActivityBusyLabel.h"

namespace {

TEST(ActivityBusyLabel, HomeSaysGoingHome) { EXPECT_EQ(activity_busy::labelFor("Home"), StrId::STR_BUSY_GOING_HOME); }

TEST(ActivityBusyLabel, EveryReaderSharesOneLabel) {
  EXPECT_EQ(activity_busy::labelFor("Reader"), StrId::STR_BUSY_OPENING_BOOK);
  EXPECT_EQ(activity_busy::labelFor("EpubReader"), StrId::STR_BUSY_OPENING_BOOK);
  EXPECT_EQ(activity_busy::labelFor("TxtReader"), StrId::STR_BUSY_OPENING_BOOK);
  EXPECT_EQ(activity_busy::labelFor("XtcReader"), StrId::STR_BUSY_OPENING_BOOK);
}

TEST(ActivityBusyLabel, NetworkScreensSayConnecting) {
  EXPECT_EQ(activity_busy::labelFor("CrossPointWebServer"), StrId::STR_BUSY_CONNECTING);
  EXPECT_EQ(activity_busy::labelFor("OpdsBookBrowser"), StrId::STR_BUSY_CONNECTING);
  EXPECT_EQ(activity_busy::labelFor("OtaUpdate"), StrId::STR_BUSY_CONNECTING);
}

TEST(ActivityBusyLabel, AnUnlistedScreenStillGetsALabel) {
  EXPECT_EQ(activity_busy::labelFor("ButtonRemap"), StrId::STR_BUSY_OPENING);
  EXPECT_EQ(activity_busy::labelFor(""), StrId::STR_BUSY_OPENING);
}

TEST(ActivityBusyLabel, SleepAndBootAreNeverCovered) {
  EXPECT_FALSE(activity_busy::wantsBanner("Sleep"));
  EXPECT_FALSE(activity_busy::wantsBanner("Boot"));
  EXPECT_TRUE(activity_busy::wantsBanner("Home"));
  EXPECT_TRUE(activity_busy::wantsBanner("Settings"));
}

}  // namespace
