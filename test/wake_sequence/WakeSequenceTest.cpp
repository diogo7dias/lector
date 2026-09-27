// The wake decision, as one asserted table.
//
// Every expectation here states TODAY'S shipped routing.
#include <gtest/gtest.h>

#include "sleep/WakeSequence.h"

namespace {

using wake_sequence::plan;
using wake_sequence::Target;
using wake_sequence::WakeInputs;

// An ordinary power-button wake from a book, nothing special set.
WakeInputs resumeWake() {
  WakeInputs in;
  in.openEpubPathEmpty = false;
  in.lastSleepFromReader = true;
  in.paintedFaceWake = true;
  return in;
}

}  // namespace

// ---------------------------------------------------------------------------
// Precedence. The order of the arms IS the policy, so it is asserted directly.
// ---------------------------------------------------------------------------

TEST(WakeSequence, RecoveryFirmwareOutranksEverything) {
  WakeInputs in = resumeWake();
  in.recoveryFirmwareMode = true;
  in.panic = true;
  in.silentReboot = true;
  EXPECT_EQ(plan(in).target, Target::RecoveryFirmware);
}

// The way back onto a device whose USB flashing the vendor locked. It must stay reachable
// even when the reader crashed last boot.
TEST(WakeSequence, RecoveryFirmwareSurvivesACrashLoop) {
  WakeInputs in;
  in.recoveryFirmwareMode = true;
  in.readerCrashed = true;
  EXPECT_EQ(plan(in).target, Target::RecoveryFirmware);
}

TEST(WakeSequence, PanicOutranksEverythingButRecovery) {
  WakeInputs in = resumeWake();
  in.panic = true;
  in.silentReboot = true;
  EXPECT_EQ(plan(in).target, Target::CrashReport);
}

// ---------------------------------------------------------------------------
// The silent-reboot arms. The second one exists specifically to NOT fall through.
// ---------------------------------------------------------------------------

TEST(WakeSequence, ASilentRebootTargetingTheReaderWithABookResumesIt) {
  WakeInputs in;
  in.silentReboot = true;
  in.silentTargetIsReader = true;
  in.openEpubPathEmpty = false;
  EXPECT_EQ(plan(in).target, Target::SilentReader);
}

TEST(WakeSequence, ASilentRebootTargetingTheReaderWithNoBookLandsOnHome) {
  WakeInputs in;
  in.silentReboot = true;
  in.silentTargetIsReader = true;
  in.openEpubPathEmpty = true;
  EXPECT_EQ(plan(in).target, Target::SilentHome);
}

// The regression this arm was written for: a silent reboot must NOT reach the sleep-wake
// resume logic, which fires on a stale openEpubPath + lastSleepFromReader left by a prior
// session. Same inputs that would otherwise resume, plus silentReboot.
TEST(WakeSequence, ASilentRebootNeverFallsThroughToTheSleepWakeResume) {
  WakeInputs in = resumeWake();
  in.silentReboot = true;
  in.silentTargetIsReader = false;
  EXPECT_EQ(plan(in).target, Target::SilentHome);
}

// ---------------------------------------------------------------------------
// Ordinary routing.
// ---------------------------------------------------------------------------

TEST(WakeSequence, WakingFromABookResumesIt) { EXPECT_EQ(plan(resumeWake()).target, Target::ResumeReader); }

TEST(WakeSequence, HoldingBackAsksForHome) {
  WakeInputs in = resumeWake();
  in.backHeld = true;
  EXPECT_EQ(plan(in).target, Target::Home);
}

TEST(WakeSequence, AReaderCrashLandsOnHomeRatherThanRetrying) {
  WakeInputs in = resumeWake();
  in.readerCrashed = true;
  EXPECT_EQ(plan(in).target, Target::Home);
}

TEST(WakeSequence, ASleepThatWasNotFromTheReaderLandsOnHome) {
  WakeInputs in = resumeWake();
  in.lastSleepFromReader = false;
  EXPECT_EQ(plan(in).target, Target::Home);
}

TEST(WakeSequence, OpenBookOnBootOpensThePickedBook) {
  WakeInputs in;
  in.bookOnBoot = true;
  in.bootBookPicked = true;
  EXPECT_EQ(plan(in).target, Target::BootBookReader);
}

// Back held or a prior crash drops the picked book rather than re-picking it, so a book
// that cannot open can never wedge boot. This exclusion used to be a mutation of
// bootBookPath inside the dispatch chain; it is a plan decision now.
TEST(WakeSequence, OpenBookOnBootYieldsToBackHeld) {
  WakeInputs in;
  in.bookOnBoot = true;
  in.bootBookPicked = true;
  in.backHeld = true;
  EXPECT_EQ(plan(in).target, Target::Home);
}

TEST(WakeSequence, OpenBookOnBootYieldsToAReaderCrash) {
  WakeInputs in;
  in.bookOnBoot = true;
  in.bootBookPicked = true;
  in.readerCrashed = true;
  EXPECT_EQ(plan(in).target, Target::Home);
}

TEST(WakeSequence, OpenBookOnBootWithNothingPickedLandsOnHome) {
  WakeInputs in;
  in.bookOnBoot = true;
  in.bootBookPicked = false;
  EXPECT_EQ(plan(in).target, Target::Home);
}

// ---------------------------------------------------------------------------
// waitBeforeRoutePaint. The rule that used to live ONLY in a comment.
// ---------------------------------------------------------------------------

// "Every route but the reader paints straight from its onEnter, so it waits out a blank
// still in flight first." The reader arms skip the wait because ReaderActivity::onEnter
// waits inside itself, after its font and book loads — waiting here would serialise work
// that currently overlaps the panel.
TEST(WakeSequence, EveryNonReaderRouteWaitsForAnInFlightBlank) {
  WakeInputs recovery;
  recovery.recoveryFirmwareMode = true;
  EXPECT_TRUE(plan(recovery).waitBeforeRoutePaint);

  WakeInputs panic;
  panic.panic = true;
  EXPECT_TRUE(plan(panic).waitBeforeRoutePaint);

  WakeInputs home = resumeWake();
  home.backHeld = true;
  EXPECT_EQ(plan(home).target, Target::Home);
  EXPECT_TRUE(plan(home).waitBeforeRoutePaint);
}

TEST(WakeSequence, NoReaderRouteWaitsBeforeDispatch) {
  EXPECT_FALSE(plan(resumeWake()).waitBeforeRoutePaint);

  WakeInputs bootBook;
  bootBook.bookOnBoot = true;
  bootBook.bootBookPicked = true;
  EXPECT_FALSE(plan(bootBook).waitBeforeRoutePaint);
}

// The two deliberate omissions. A silent reboot never issues the blank in the first place
// (paintedFaceWake is false whenever resume is Silent), so neither silent arm waits —
// including SilentHome, which is otherwise a non-reader route and would look like a bug.
TEST(WakeSequence, NeitherSilentArmWaitsBecauseASilentRebootNeverBlanks) {
  WakeInputs reader;
  reader.silentReboot = true;
  reader.silentTargetIsReader = true;
  reader.openEpubPathEmpty = false;
  EXPECT_EQ(plan(reader).target, Target::SilentReader);
  EXPECT_FALSE(plan(reader).waitBeforeRoutePaint);

  WakeInputs home;
  home.silentReboot = true;
  EXPECT_EQ(plan(home).target, Target::SilentHome);
  EXPECT_FALSE(plan(home).waitBeforeRoutePaint);
}

// ---------------------------------------------------------------------------
// The reader side effects. Order matters on device: the counter must be committed BEFORE
// the reader is entered, or a crash while opening retries forever.
// ---------------------------------------------------------------------------

TEST(WakeSequence, EveryReaderArmBumpsTheCrashLoopCounter) {
  EXPECT_TRUE(plan(resumeWake()).bumpReaderLoadCount);

  WakeInputs bootBook;
  bootBook.bookOnBoot = true;
  bootBook.bootBookPicked = true;
  EXPECT_TRUE(plan(bootBook).bumpReaderLoadCount);
}

// The silent-reader arm is the exception, and always was: it neither bumps the counter nor
// clears openEpubPath. Pinned so the asymmetry is deliberate rather than forgotten.
TEST(WakeSequence, TheSilentReaderArmTouchesNeitherTheCounterNorTheOpenPath) {
  WakeInputs in;
  in.silentReboot = true;
  in.silentTargetIsReader = true;
  in.openEpubPathEmpty = false;
  const auto out = plan(in);
  EXPECT_FALSE(out.bumpReaderLoadCount);
  EXPECT_FALSE(out.clearOpenEpubPath);
  EXPECT_FALSE(out.readerTakesWakeFlags);
}

// Only the arm that resumes a painted-face wake carries the wake flags; the boot-book
// arm opens a book the user did not fall asleep in, so it paints normally.
TEST(WakeSequence, OnlyTheWakeResumingArmsCarryTheWakeFlags) {
  EXPECT_TRUE(plan(resumeWake()).readerTakesWakeFlags);

  WakeInputs bootBook;
  bootBook.bookOnBoot = true;
  bootBook.bootBookPicked = true;
  EXPECT_FALSE(plan(bootBook).readerTakesWakeFlags);
}

// A book path that is about to be cleared is also the path being opened, so the executor
// must copy it. Pinned here because the two facts only make sense together.
TEST(WakeSequence, TheArmsThatClearTheOpenPathAreTheOnesThatOpenIt) {
  EXPECT_TRUE(plan(resumeWake()).clearOpenEpubPath);
}

// No non-reader arm may carry a reader side effect.
TEST(WakeSequence, NoHomeRouteCarriesAReaderSideEffect) {
  WakeInputs home = resumeWake();
  home.backHeld = true;
  const auto out = plan(home);
  EXPECT_EQ(out.target, Target::Home);
  EXPECT_FALSE(out.bumpReaderLoadCount);
  EXPECT_FALSE(out.clearOpenEpubPath);
  EXPECT_FALSE(out.readerTakesWakeFlags);
}

// ---------------------------------------------------------------------------
// The clear strategy, including the state that was previously untestable.
// ---------------------------------------------------------------------------

TEST(WakeSequence, DriveAllIsOnlyArmedOnAFastUnlockWake) {
  WakeInputs in;
  in.paintedFaceWake = true;
  in.fastUnlock = true;
  EXPECT_EQ(wake_sequence::clearStrategy(in), wake_face::WakeClear::DriveAll);
  EXPECT_TRUE(wake_sequence::armsDriveAll(in));
  EXPECT_FALSE(wake_sequence::armsAsyncBlank(in));
}

TEST(WakeSequence, WithoutFastUnlockTheClearingBlankIsArmedInstead) {
  WakeInputs in;
  in.paintedFaceWake = true;
  in.fastUnlock = false;
  EXPECT_EQ(wake_sequence::clearStrategy(in), wake_face::WakeClear::Blank);
  EXPECT_FALSE(wake_sequence::armsDriveAll(in));
  EXPECT_TRUE(wake_sequence::armsAsyncBlank(in));
}

// Not a painted-face wake (a flash, a USB boot, a plain restart): no sleep face is on the
// glass, so there is nothing to clear.
TEST(WakeSequence, ANonPaintedFaceWakeArmsNothing) {
  WakeInputs in;
  in.paintedFaceWake = false;
  in.fastUnlock = true;
  EXPECT_EQ(wake_sequence::clearStrategy(in), wake_face::WakeClear::Blank);
  EXPECT_FALSE(wake_sequence::armsDriveAll(in));
  EXPECT_FALSE(wake_sequence::armsAsyncBlank(in));
}
