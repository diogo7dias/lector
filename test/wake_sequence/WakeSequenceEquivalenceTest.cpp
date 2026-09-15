// Equivalence check, kept as a test: wake_sequence::plan() against a literal
// transcription of the if/else chain that used to live in setup(), over EVERY combination
// of its boolean inputs.
//
// This is the only honest way to claim a refactor of this path is behaviour-preserving.
// The original was a six-arm chain wrapped in a ternary override, re-reading the same raw
// inputs three and four times; reasoning about it arm by arm is exactly how a path gets
// silently dropped. 2^11 combinations is cheap, so the machine checks it instead.
//
// The reference below is transcribed from origin/main's setup(), NOT from the new Module.
// If it is ever made to agree with plan() by editing the reference, this test is worthless.
#include <gtest/gtest.h>

#include "sleep/WakeSequence.h"

namespace {

using wake_sequence::plan;
using wake_sequence::Target;
using wake_sequence::WakeInputs;

// The shipped chain, verbatim in shape:
//
//   forcedRoute = (recovery || panic || silent) ? Unchanged : wake_route::resolve(...)
//   if      (recoveryFirmwareMode)                          -> firmware picker  [waits]
//   else if (rebootedFromPanic)                             -> crash report     [waits]
//   else if (silent && targetReader && !openEpubPath.empty) -> goToReader
//   else if (silent)                                        -> goHome
//   else if (forcedRoute == ForceReader)                    -> goToReader(flags)
//   else if (forcedRoute == ForceHome)                      -> goHome           [waits]
//   else if (bookOnBoot || openEmpty || !fromReader || backHeld || crashed) {
//              if (backHeld || crashed) bootBookPath.clear();
//              if (!bootBookPath.empty()) -> goToReader(bootBook)
//              else                       -> goHome          [waits]
//        }
//   else                                                    -> goToReader(flags)
struct Reference {
  Target target;
  bool waits;
  bool bumps;
  bool clears;
  bool wakeFlags;
};

Reference referenceChain(const WakeInputs& in) {
  wake_route::WakeInputs routeIn;
  routeIn.forceBookOnWake = in.forceBookOnWake;
  routeIn.hasBook = in.hasForcedBook;
  routeIn.sleptFromReader = in.lastSleepFromReader;
  routeIn.backHeld = in.backHeld;
  routeIn.bookOnBoot = in.bookOnBoot;
  routeIn.readerCrashed = in.readerCrashed;
  const wake_route::Route forcedRoute = (in.recoveryFirmwareMode || in.panic || in.silentReboot)
                                            ? wake_route::Route::Unchanged
                                            : wake_route::resolve(routeIn);

  if (in.recoveryFirmwareMode) return {Target::RecoveryFirmware, true, false, false, false};
  if (in.panic) return {Target::CrashReport, true, false, false, false};
  if (in.silentReboot && in.silentTargetIsReader && !in.openEpubPathEmpty)
    return {Target::SilentReader, false, false, false, false};
  if (in.silentReboot) return {Target::SilentHome, false, false, false, false};
  if (forcedRoute == wake_route::Route::ForceReader) return {Target::ForcedReader, false, true, true, true};
  if (forcedRoute == wake_route::Route::ForceHome) return {Target::ForcedHome, true, false, false, false};
  if (in.bookOnBoot || in.openEpubPathEmpty || !in.lastSleepFromReader || in.backHeld || in.readerCrashed) {
    bool bootBookPicked = in.bootBookPicked;
    if (in.backHeld || in.readerCrashed) bootBookPicked = false;  // bootBookPath.clear()
    if (bootBookPicked) return {Target::BootBookReader, false, true, false, false};
    return {Target::Home, true, false, false, false};
  }
  return {Target::ResumeReader, false, true, true, true};
}

}  // namespace

TEST(WakeSequenceEquivalence, PlanMatchesTheOriginalChainForEveryInputCombination) {
  // The eleven booleans the chain actually branches on.
  int checked = 0;
  for (int bits = 0; bits < (1 << 11); ++bits) {
    WakeInputs in;
    in.recoveryFirmwareMode = bits & (1 << 0);
    in.panic = bits & (1 << 1);
    in.silentReboot = bits & (1 << 2);
    in.silentTargetIsReader = bits & (1 << 3);
    in.forceBookOnWake = bits & (1 << 4);
    in.hasForcedBook = bits & (1 << 5);
    in.openEpubPathEmpty = bits & (1 << 6);
    in.lastSleepFromReader = bits & (1 << 7);
    in.backHeld = bits & (1 << 8);
    in.bookOnBoot = bits & (1 << 9);
    in.readerCrashed = bits & (1 << 10);
    // bootBookPicked is the twelfth; exercise both values against every other combination.
    for (const bool picked : {false, true}) {
      in.bootBookPicked = picked;
      const Reference want = referenceChain(in);
      const wake_sequence::WakePlan got = plan(in);
      ASSERT_EQ(static_cast<int>(got.target), static_cast<int>(want.target)) << "bits=" << bits << " picked=" << picked;
      ASSERT_EQ(got.waitBeforeRoutePaint, want.waits) << "bits=" << bits << " picked=" << picked;
      ASSERT_EQ(got.bumpReaderLoadCount, want.bumps) << "bits=" << bits << " picked=" << picked;
      ASSERT_EQ(got.clearOpenEpubPath, want.clears) << "bits=" << bits << " picked=" << picked;
      ASSERT_EQ(got.readerTakesWakeFlags, want.wakeFlags) << "bits=" << bits << " picked=" << picked;
      ++checked;
    }
  }
  EXPECT_EQ(checked, 4096);
}
