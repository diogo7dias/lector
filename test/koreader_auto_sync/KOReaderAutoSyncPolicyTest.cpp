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

TEST(KOReaderAutoSyncPolicy, BookKeyDistinguishesTwoBooks) {
  EXPECT_NE(ko_auto_sync::bookKey("/books/dune.epub"), ko_auto_sync::bookKey("/books/emma.epub"));
}

TEST(KOReaderAutoSyncPolicy, BookKeyIsStableForTheSamePath) {
  EXPECT_EQ(ko_auto_sync::bookKey("/books/dune.epub"), ko_auto_sync::bookKey("/books/dune.epub"));
}

TEST(KOReaderAutoSyncPolicy, BookKeyIsNeverZeroSoItCannotLookLikeAnEmptyRecord) {
  EXPECT_NE(ko_auto_sync::bookKey(""), 0u);
}

TEST(KOReaderAutoSyncPolicy, PullsAgainForABookThisWakeHasNotSeen) {
  ko_auto_sync::PullMemory memory;
  memory.magic = ko_auto_sync::kPullMemoryMagic;
  memory.bookKey = ko_auto_sync::bookKey("/books/dune.epub");
  EXPECT_FALSE(ko_auto_sync::alreadyPulledThisWake(memory, ko_auto_sync::bookKey("/books/emma.epub")));
}

TEST(KOReaderAutoSyncPolicy, DoesNotPullTwiceForTheSameBookWithinOneWake) {
  // Stepping out to the library and straight back into the same book must not cost a
  // second network round trip; nothing can have moved on the other device in between.
  ko_auto_sync::PullMemory memory;
  memory.magic = ko_auto_sync::kPullMemoryMagic;
  memory.bookKey = ko_auto_sync::bookKey("/books/dune.epub");
  EXPECT_TRUE(ko_auto_sync::alreadyPulledThisWake(memory, ko_auto_sync::bookKey("/books/dune.epub")));
}

TEST(KOReaderAutoSyncPolicy, UnstampedPullMemoryIsIgnored) {
  // The record lives in RTC memory, which is not zeroed on a software reset. Without the
  // stamp, leftover bytes could suppress a pull the user is owed.
  ko_auto_sync::PullMemory memory;
  memory.magic = 0;
  memory.bookKey = ko_auto_sync::bookKey("/books/dune.epub");
  EXPECT_FALSE(ko_auto_sync::alreadyPulledThisWake(memory, ko_auto_sync::bookKey("/books/dune.epub")));
}

TEST(KOReaderAutoSyncPolicy, PendingPullOnlyAppliesToTheBookItWasFetchedFor) {
  // The handoff survives a reboot. If anything but the intended book opens on the other
  // side of it, applying the position would drop the reader somewhere arbitrary.
  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.xpathLength = 12;
  pending.bookKey = ko_auto_sync::bookKey("/books/dune.epub");
  EXPECT_TRUE(ko_auto_sync::pendingPullMatchesBook(pending, ko_auto_sync::bookKey("/books/dune.epub")));
  EXPECT_FALSE(ko_auto_sync::pendingPullMatchesBook(pending, ko_auto_sync::bookKey("/books/emma.epub")));
}

TEST(KOReaderAutoSyncPolicy, InvalidPendingPullMatchesNoBook) {
  ko_auto_sync::PendingPull pending;
  pending.magic = 0;
  pending.bookKey = ko_auto_sync::bookKey("/books/dune.epub");
  EXPECT_FALSE(ko_auto_sync::pendingPullMatchesBook(pending, ko_auto_sync::bookKey("/books/dune.epub")));
}

TEST(KOReaderAutoSyncPolicy, ClockIsUsableFromTwoThousandTwentyFourOnwards) {
  // TLS checks certificate validity against the clock, so a device that booted without a
  // set time has to learn it before the handshake; one that already knows must not wait.
  EXPECT_FALSE(ko_auto_sync::clockLooksSet(0));
  EXPECT_FALSE(ko_auto_sync::clockLooksSet(1000000000));  // 2001
  EXPECT_TRUE(ko_auto_sync::clockLooksSet(1767225600));   // 2026
}

TEST(KOReaderAutoSyncPolicy, PendingPullWithAnEmptyXpathIsInvalid) {
  // Nothing to resolve means nothing to apply, so the reader must open normally
  // instead of showing a resume banner for a position it cannot reach.
  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.xpathLength = 0;
  EXPECT_FALSE(ko_auto_sync::isPendingPullValid(pending));
}
