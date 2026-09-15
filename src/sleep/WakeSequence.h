#pragma once

// Pure policy: where does a wake land, and what does the executor have to do on the way?
//
// setup() used to make this decision three times over the same raw inputs:
//   - wake_route::resolve() was called, then IMMEDIATELY overridden inline by a
//     recovery/panic/silent ternary;
//   - a six-arm if/else chain then re-tested isPressed(Back) and readerActivityLoadCount,
//     which had already been read twice above it.
// Nothing tied the three together, so the tested helper could be contradicted by the two
// untested layers wrapped around it.
//
// plan() is that whole decision, once. The five policy helpers that used to be called
// separately are private details of it now, which is what makes the contradiction
// impossible rather than merely unlikely.
//
// Math core only: no SD, no framebuffer, no settings object, no hardware — so every row
// below is host-testable. See test/wake_sequence.

#include <cstdint>

#include "WakeFacePolicy.h"
#include "WakeRoutePolicy.h"

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
  // The sleep face named a book and must open it (wake_route::Route::ForceReader).
  ForcedReader,
  // The forced-book path hit a safety valve — no book, or the reader crashed last boot.
  ForcedHome,
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

  // The forced-book promise from the sleep face.
  bool forceBookOnWake = false;
  bool hasForcedBook = false;

  // Ordinary routing inputs. Each of these was read three or four separate times by the
  // chain this replaces; here each is read once.
  bool openEpubPathEmpty = true;
  bool lastSleepFromReader = false;
  bool backHeld = false;
  bool bookOnBoot = false;
  // readerActivityLoadCount > 0: the reader failed to come up last boot. The crash-loop
  // guard's input.
  bool readerCrashed = false;
  // A boot book was picked earlier in setup() (the unlock banner already named it).
  bool bootBookPicked = false;

  // Display state, for waitBeforeRoutePaint.
  bool paintedFaceWake = false;
  bool fastUnlock = false;
  bool wakeStraightToBook = false;
  bool asyncBlankInFlight = false;
  bool driveAllArmed = false;
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
  // Pass allowFastInitialReaderRefresh / firstTurnCleans to goToReader. Only the two arms
  // that resume a painted-face wake do.
  bool readerTakesWakeFlags = false;
};

// How a wake from a painted sleep face gets the page onto the panel. Unchanged rule,
// moved here so the arming decision and the routing decision are read off one value.
inline constexpr wake_face::WakeClear clearStrategy(const WakeInputs& in) {
  return in.paintedFaceWake ? wake_face::wakeClearFor(in.fastUnlock, in.wakeStraightToBook)
                            : wake_face::WakeClear::Blank;
}

// Whether the DriveAll one-shot is actually armed. Note the extra `wakeStraightToBook`
// gate: wakeClearFor can answer DriveAll while this answers false, so "DriveAll computed
// but never armed" is a real state. It was unreachable by any test before, because the
// gate sat inline in setup() around a call to the tested helper.
inline constexpr bool armsDriveAll(const WakeInputs& in) {
  return in.paintedFaceWake && in.wakeStraightToBook && clearStrategy(in) == wake_face::WakeClear::DriveAll;
}

inline constexpr bool armsAsyncBlank(const WakeInputs& in) {
  return in.paintedFaceWake && in.wakeStraightToBook && clearStrategy(in) == wake_face::WakeClear::Blank;
}

inline constexpr WakePlan plan(const WakeInputs& in) {
  WakePlan out;

  // The forced-book route, with its overrides folded IN rather than applied to it
  // afterwards. Recovery, panic and a silent reboot each outrank the sleep face's promise;
  // previously they were a ternary wrapped around the call to wake_route::resolve, so the
  // tested function's answer could be discarded by untested code one line later.
  wake_route::WakeInputs routeIn;
  routeIn.forceBookOnWake = in.forceBookOnWake;
  routeIn.hasBook = in.hasForcedBook;
  routeIn.sleptFromReader = in.lastSleepFromReader;
  routeIn.backHeld = in.backHeld;
  routeIn.bookOnBoot = in.bookOnBoot;
  routeIn.readerCrashed = in.readerCrashed;
  const bool overridden = in.recoveryFirmwareMode || in.panic || in.silentReboot;
  const wake_route::Route forced = overridden ? wake_route::Route::Unchanged : wake_route::resolve(routeIn);

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
  if (forced == wake_route::Route::ForceReader) {
    out.target = Target::ForcedReader;
    out.bumpReaderLoadCount = true;
    out.clearOpenEpubPath = true;
    out.readerTakesWakeFlags = true;
    return out;
  }
  if (forced == wake_route::Route::ForceHome) {
    out.target = Target::ForcedHome;
    out.waitBeforeRoutePaint = true;
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
