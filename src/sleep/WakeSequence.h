#pragma once

// Pure policy: where does a wake land, and what does the executor have to do on the way?
//
// plan() is the whole decision, once, over raw inputs each read a single time, so no
// untested layer around it can contradict it.
//
// Math core only: no SD, no framebuffer, no settings object, no hardware — so every row
// below is host-testable. See test/wake_sequence.

#include <cstdint>

#include "WakeFacePolicy.h"

namespace wake_sequence {

// Where the wake lands. One value per arm of the chain this replaces, in the same order
// the chain tested them, because the order IS the precedence.
enum class Target : uint8_t {
  // Hold a side button with power at boot: the SD firmware picker. The way back onto a
  // device whose USB flashing the vendor locked, so it outranks everything.
  RecoveryFirmware,
  // Rebooted from a panic: the crash report, so the capture is seen before anything
  // overwrites it.
  CrashReport,
  // Silent reboot that asked for the reader and has a book to open.
  SilentReader,
  // Silent reboot, anything else. Deliberately NOT falling through to the sleep-wake
  // "resume reader" logic below, which fires on a stale openEpubPath + lastSleepFromReader
  // left by a prior session.
  SilentHome,
  // "Open Book on Boot": jump straight into the last-read book, or one in progress.
  BootBookReader,
  // The ordinary landing.
  Home,
  // Woke from a book: resume it.
  ResumeReader,
};

struct WakeInputs {
  // Boot classification. panic must be LATCHED before HalSystem::checkPanic() clears the
  // marker mid-setup; see the assertion in the test.
  bool recoveryFirmwareMode = false;
  bool panic = false;
  bool silentReboot = false;
  bool silentTargetIsReader = false;

  // Ordinary routing inputs. Each of these was read three or four separate times by the
  // chain this replaces; here each is read once.
  bool openEpubPathEmpty = true;
  bool lastSleepFromReader = false;
  bool backHeld = false;
  bool bookOnBoot = false;
  // readerActivityLoadCount > 0: the reader failed to come up last boot. The crash-loop
  // guard's input.
  bool readerCrashed = false;
  // A boot book was picked earlier in setup().
  bool bootBookPicked = false;

  // Display state, for clearStrategy() and the arming it decides.
  bool paintedFaceWake = false;
  bool fastUnlock = false;
};

struct WakePlan {
  Target target = Target::Home;

  // Whether setup() must call renderer.waitRefreshComplete() before dispatching.
  //
  // This was the most dangerous thing on the path: the rule lived ONLY in a comment
  // ("Every route but the reader paints straight from its onEnter"), realised as five
  // scattered call sites plus two deliberate omissions. A new arm that forgot the call
  // was a silent double-paint with nothing to catch it. As a field it is an asserted row
  // in a table instead of an omission a future author has to notice.
  //
  // The reader arms skip it because ReaderActivity::onEnter waits inside itself, after
  // its font and book loads — waiting here would serialise work that currently overlaps
  // the panel. The silent-reboot arms skip it because a silent reboot never issues the
  // blank in the first place (paintedFaceWake is false whenever resume is Silent).
  bool waitBeforeRoutePaint = false;

  // Bump readerActivityLoadCount and commit APP_STATE before entering the reader. The
  // crash-loop guard: a crash while opening then lands on home next boot instead of
  // retrying forever. MUST happen before goToReader, which is the whole point.
  bool bumpReaderLoadCount = false;
  // Clear APP_STATE.openEpubPath before entering, so a book that cannot load cannot wedge
  // boot. Not done on the silent-reader and boot-book arms, which never cleared it.
  bool clearOpenEpubPath = false;
  // Pass allowFastInitialReaderRefresh / firstTurnCleans to goToReader. Only the arm that
  // resumes a painted-face wake does.
  bool readerTakesWakeFlags = false;
};

// How a wake from a painted sleep face gets the page onto the panel. Unchanged rule,
// moved here so the arming decision and the routing decision are read off one value.
inline constexpr wake_face::WakeClear clearStrategy(const WakeInputs& in) {
  return in.paintedFaceWake ? wake_face::wakeClearFor(in.fastUnlock) : wake_face::WakeClear::Blank;
}

// Whether the DriveAll one-shot is armed for a painted-face wake.
inline constexpr bool armsDriveAll(const WakeInputs& in) {
  return in.paintedFaceWake && clearStrategy(in) == wake_face::WakeClear::DriveAll;
}

inline constexpr bool armsAsyncBlank(const WakeInputs& in) {
  return in.paintedFaceWake && clearStrategy(in) == wake_face::WakeClear::Blank;
}

inline constexpr WakePlan plan(const WakeInputs& in) {
  WakePlan out;

  if (in.recoveryFirmwareMode) {
    out.target = Target::RecoveryFirmware;
    out.waitBeforeRoutePaint = true;
    return out;
  }
  if (in.panic) {
    out.target = Target::CrashReport;
    out.waitBeforeRoutePaint = true;
    return out;
  }
  if (in.silentReboot && in.silentTargetIsReader && !in.openEpubPathEmpty) {
    out.target = Target::SilentReader;
    return out;
  }
  if (in.silentReboot) {
    out.target = Target::SilentHome;
    return out;
  }
  // Boot to home when no book is open, the last sleep was not from the reader, Back is
  // held (the user is asking for home), or the reader crashed last boot — and "Open Book
  // on Boot" turns that home into a book instead, unless Back or a prior crash says no.
  if (in.bookOnBoot || in.openEpubPathEmpty || !in.lastSleepFromReader || in.backHeld || in.readerCrashed) {
    // Back held or a prior reader crash drops the picked book rather than re-picking, so
    // a book that cannot open can never wedge boot.
    const bool useBootBook = in.bootBookPicked && !in.backHeld && !in.readerCrashed;
    if (useBootBook) {
      out.target = Target::BootBookReader;
      out.bumpReaderLoadCount = true;
      return out;
    }
    out.target = Target::Home;
    out.waitBeforeRoutePaint = true;
    return out;
  }
  out.target = Target::ResumeReader;
  out.bumpReaderLoadCount = true;
  out.clearOpenEpubPath = true;
  out.readerTakesWakeFlags = true;
  return out;
}

}  // namespace wake_sequence
