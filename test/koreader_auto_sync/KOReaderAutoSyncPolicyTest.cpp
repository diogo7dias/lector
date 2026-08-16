#include <gtest/gtest.h>

#include "activities/reader/KOReaderAutoSyncPolicy.h"

namespace {

ko_auto_sync::Gate readyGate() {
  ko_auto_sync::Gate gate;
  gate.autoSyncEnabled = true;
  gate.hasSyncCredentials = true;
  gate.hasWifiCredentials = true;
  return gate;
}

}  // namespace

TEST(KOReaderAutoSyncPolicy, PushesOnPowerButtonSleepFromTheReader) {
  EXPECT_TRUE(ko_auto_sync::shouldPushOnSleep(readyGate(), ko_auto_sync::SleepCause::PowerButton, true));
}

TEST(KOReaderAutoSyncPolicy, DoesNotPushOnInactivityTimeoutSleep) {
  // A timeout sleep is unattended: spending 4 seconds of radio time on it would
  // drain the battery every time the device is left open on a page.
  EXPECT_FALSE(ko_auto_sync::shouldPushOnSleep(readyGate(), ko_auto_sync::SleepCause::InactivityTimeout, true));
}

TEST(KOReaderAutoSyncPolicy, DoesNotPushOnSleepOutsideTheReader) {
  EXPECT_FALSE(ko_auto_sync::shouldPushOnSleep(readyGate(), ko_auto_sync::SleepCause::PowerButton, false));
}

TEST(KOReaderAutoSyncPolicy, DoesNotPushWhenAutoSyncIsOff) {
  ko_auto_sync::Gate gate = readyGate();
  gate.autoSyncEnabled = false;
  EXPECT_FALSE(ko_auto_sync::shouldPushOnSleep(gate, ko_auto_sync::SleepCause::PowerButton, true));
  EXPECT_FALSE(ko_auto_sync::shouldPushOnLeavingBook(gate));
  EXPECT_FALSE(ko_auto_sync::shouldPullOnBookOpen(gate));
}

TEST(KOReaderAutoSyncPolicy, DoesNotSyncWithoutSyncCredentials) {
  ko_auto_sync::Gate gate = readyGate();
  gate.hasSyncCredentials = false;
  EXPECT_FALSE(ko_auto_sync::shouldPushOnSleep(gate, ko_auto_sync::SleepCause::PowerButton, true));
  EXPECT_FALSE(ko_auto_sync::shouldPushOnLeavingBook(gate));
  EXPECT_FALSE(ko_auto_sync::shouldPullOnBookOpen(gate));
}

TEST(KOReaderAutoSyncPolicy, DoesNotSyncWithoutASavedWifiNetwork) {
  // Auto sync never opens the network picker: with nothing saved there is no
  // network to join unattended.
  ko_auto_sync::Gate gate = readyGate();
  gate.hasWifiCredentials = false;
  EXPECT_FALSE(ko_auto_sync::shouldPushOnSleep(gate, ko_auto_sync::SleepCause::PowerButton, true));
  EXPECT_FALSE(ko_auto_sync::shouldPushOnLeavingBook(gate));
  EXPECT_FALSE(ko_auto_sync::shouldPullOnBookOpen(gate));
}

TEST(KOReaderAutoSyncPolicy, PushesWhenLeavingTheBookAndPullsWhenOpeningIt) {
  EXPECT_TRUE(ko_auto_sync::shouldPushOnLeavingBook(readyGate()));
  EXPECT_TRUE(ko_auto_sync::shouldPullOnBookOpen(readyGate()));
}

TEST(KOReaderAutoSyncPolicy, RemoteWinsWhenItIsFurtherThanLocal) {
  EXPECT_TRUE(ko_auto_sync::remoteIsFurther(0.20f, 0.50f));
}

TEST(KOReaderAutoSyncPolicy, LocalWinsWhenItIsFurtherThanRemote) {
  EXPECT_FALSE(ko_auto_sync::remoteIsFurther(0.50f, 0.20f));
}

TEST(KOReaderAutoSyncPolicy, TreatsProgressWithinOneTenthOfAPercentAsTheSamePlace) {
  // Matches the epsilon the manual smart sync already uses, so a rounding
  // difference between two devices does not bounce the reader by a page.
  EXPECT_FALSE(ko_auto_sync::remoteIsFurther(0.5000f, 0.5008f));
  EXPECT_TRUE(ko_auto_sync::remoteIsFurther(0.5000f, 0.5020f));
}

TEST(KOReaderAutoSyncPolicy, PendingPullIsInvalidWithoutTheMagicStamp) {
  ko_auto_sync::PendingPull pending;
  pending.magic = 0;
  pending.xpathLength = 4;
  EXPECT_FALSE(ko_auto_sync::isPendingPullValid(pending));
}

TEST(KOReaderAutoSyncPolicy, PendingPullIsInvalidWhenTheXpathOverrunsItsBuffer) {
  // The struct lives in RTC memory across a reboot, where nothing zeroes it.
  // A corrupt length must not be trusted as a string bound.
  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.xpathLength = ko_auto_sync::kMaxXPathLength + 1;
  EXPECT_FALSE(ko_auto_sync::isPendingPullValid(pending));
}

TEST(KOReaderAutoSyncPolicy, PendingPullIsValidWithTheMagicStampAndAFittingXpath) {
  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.xpathLength = 32;
  EXPECT_TRUE(ko_auto_sync::isPendingPullValid(pending));
}

TEST(KOReaderAutoSyncPolicy, PendingPullWithAnEmptyXpathIsInvalid) {
  // Nothing to resolve means nothing to apply, so the reader must open normally
  // instead of showing a resume banner for a position it cannot reach.
  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.xpathLength = 0;
  EXPECT_FALSE(ko_auto_sync::isPendingPullValid(pending));
}
