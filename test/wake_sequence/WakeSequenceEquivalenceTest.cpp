// Equivalence check, kept as a test: wake_sequence::plan() against a literal
// transcription of the if/else chain that used to live in setup(), over EVERY combination
// of its inputs: each boolean, and each silent-reboot target.
//
// This is the only honest way to claim a refactor of this path is behaviour-preserving.
// The original was a six-arm chain wrapped in a ternary override, re-reading the same raw
// inputs three and four times; reasoning about it arm by arm is exactly how a path gets
// silently dropped. The combinations are cheap, so the machine checks them instead.
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
//   if      (recoveryFirmwareMode)                          -> firmware picker  [waits]
//   else if (rebootedFromPanic)                             -> crash report     [waits]
//   else if (silent && targetReader && !openEpubPath.empty) -> goToReader
//   else if (silent && targetSettings)                      -> goSettings
//   else if (silent)                                        -> goHome
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
  if (in.recoveryFirmwareMode) return {Target::RecoveryFirmware, true, false, false, false};
  if (in.panic) return {Target::CrashReport, true, false, false, false};
  if (in.silentReboot && in.silentTarget == RestartTarget::Reader && !in.openEpubPathEmpty)
    return {Target::SilentReader, false, false, false, false};
  if (in.silentReboot && in.silentTarget == RestartTarget::Settings)
    return {Target::SilentSettings, false, false, false, false};
  if (in.silentReboot) return {Target::SilentHome, false, false, false, false};
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
  // Eight booleans from the bit mask, bootBookPicked on top, and the silent-reboot target,
  // which is one value rather than two flags.
  int checked = 0;
  for (int bits = 0; bits < (1 << 8); ++bits) {
    for (const RestartTarget target : {RestartTarget::Home, RestartTarget::Reader, RestartTarget::Settings}) {
      WakeInputs in;
      in.recoveryFirmwareMode = bits & (1 << 0);
      in.panic = bits & (1 << 1);
      in.silentReboot = bits & (1 << 2);
      in.silentTarget = target;
      in.openEpubPathEmpty = bits & (1 << 3);
      in.lastSleepFromReader = bits & (1 << 4);
      in.backHeld = bits & (1 << 5);
      in.bookOnBoot = bits & (1 << 6);
      in.readerCrashed = bits & (1 << 7);
      // bootBookPicked is the last one; exercise both values against every other combination.
      for (const bool picked : {false, true}) {
        in.bootBookPicked = picked;
        const Reference want = referenceChain(in);
        const wake_sequence::WakePlan got = plan(in);
        ASSERT_EQ(static_cast<int>(got.target), static_cast<int>(want.target))
            << "bits=" << bits << " target=" << static_cast<int>(target) << " picked=" << picked;
        ASSERT_EQ(got.waitBeforeRoutePaint, want.waits) << "bits=" << bits << " picked=" << picked;
        ASSERT_EQ(got.bumpReaderLoadCount, want.bumps) << "bits=" << bits << " picked=" << picked;
        ASSERT_EQ(got.clearOpenEpubPath, want.clears) << "bits=" << bits << " picked=" << picked;
        ASSERT_EQ(got.readerTakesWakeFlags, want.wakeFlags) << "bits=" << bits << " picked=" << picked;
        ++checked;
      }
    }
  }
  EXPECT_EQ(checked, 3 * 2 * (1 << 8));
}
