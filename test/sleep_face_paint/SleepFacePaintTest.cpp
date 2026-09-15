// The sleep-face paint table, asserted per face per device.
//
// Every expectation here states TODAY'S shipped behaviour. A refresh waveform and the
// NUMBER of panel submissions are physically observable on e-ink — wrong choices show as
// ghosting, blotchy grey or visible blinking — and retained charge means neither can be
// verified by reading code. So this file is a record of what the panel currently receives,
// not an opinion about what it should receive. A row that changes here is a hardware
// change and needs a device check first.
//
// Product-locked: exactly three faces exist (wallpaper, book-cover fallback, plain white
// "Lector"). The wallpaper path is the priority path.
#include <gtest/gtest.h>

#include "activities/boot_sleep/SleepFacePaint.h"

namespace {

using sleep_face::Face;
using sleep_face::PaintPlan;
using sleep_face::planFor;
using sleep_face::totalSubmissions;

DeviceProfile uc8279X3() {
  DeviceProfile d;
  d.isX3 = true;
  d.controllerIsUc8279 = true;
  return d;
}

DeviceProfile uc8279X4() {
  DeviceProfile d;
  d.controllerIsUc8279 = true;
  return d;
}

DeviceProfile ssd1677X4Pro() {
  DeviceProfile d;
  d.isX4Pro = true;
  d.hasTouch = true;
  d.controllerIsUc8279 = false;
  return d;
}

struct Row {
  const char* name;
  DeviceProfile dev;
};

const Row kDevices[] = {
    {"UC8279 X3", uc8279X3()},
    {"UC8279 X4", uc8279X4()},
    {"SSD1677 X4 Pro", ssd1677X4Pro()},
};

}  // namespace

// ---------------------------------------------------------------------------
// Submission counts. These were emergent before this Module existed: you had to read
// three render functions and a bool to count them, and the comment in SleepActivity
// claimed "every lock is two panel submissions" for all of them, which was false.
// ---------------------------------------------------------------------------

TEST(SleepFacePaint, AToneCarryingWallpaperIsTwoSubmissionsOnEveryDevice) {
  for (const Row& row : kDevices) {
    const PaintPlan plan = planFor(Face::Wallpaper, /*sourceHasGrayscale=*/true, row.dev);
    EXPECT_EQ(plan.submissions, 2) << row.name;
    EXPECT_TRUE(plan.grayscalePlanes) << row.name;
    // Popup, then base, then the two grayscale planes committed together.
    EXPECT_EQ(totalSubmissions(Face::Wallpaper, true, row.dev), 3) << row.name;
  }
}

// A .bmp wallpaper whose tone did not survive the cover filter, or which never had any.
TEST(SleepFacePaint, AFlattenedWallpaperIsOneSubmissionOnEveryDevice) {
  for (const Row& row : kDevices) {
    const PaintPlan plan = planFor(Face::Wallpaper, /*sourceHasGrayscale=*/false, row.dev);
    EXPECT_EQ(plan.submissions, 1) << row.name;
    EXPECT_FALSE(plan.grayscalePlanes) << row.name;
    EXPECT_EQ(plan.base, HalDisplay::HALF_REFRESH) << row.name;
    EXPECT_EQ(totalSubmissions(Face::Wallpaper, false, row.dev), 2) << row.name;
  }
}

// The cover fallback renders through the very same bitmap path, so it shares the rows.
// This is the 3-or-2 the review flagged: the count depends on the SOURCE, not the face.
TEST(SleepFacePaint, TheCoverFallbackSharesTheWallpaperRows) {
  for (const Row& row : kDevices) {
    EXPECT_EQ(planFor(Face::CoverFallback, true, row.dev).submissions,
              planFor(Face::Wallpaper, true, row.dev).submissions)
        << row.name;
    EXPECT_EQ(planFor(Face::CoverFallback, true, row.dev).base, planFor(Face::Wallpaper, true, row.dev).base)
        << row.name;
    EXPECT_EQ(planFor(Face::CoverFallback, false, row.dev).submissions, 1) << row.name;
  }
}

// The plain white "Lector" page. One clean pass, never the grayscale pipeline, whatever
// the caller claims about the source.
TEST(SleepFacePaint, ThePlainLectorFaceIsAlwaysOneHalfSubmission) {
  for (const Row& row : kDevices) {
    for (const bool hasTone : {false, true}) {
      const PaintPlan plan = planFor(Face::PlainLector, hasTone, row.dev);
      EXPECT_EQ(plan.base, HalDisplay::HALF_REFRESH) << row.name;
      EXPECT_EQ(plan.submissions, 1) << row.name;
      EXPECT_FALSE(plan.grayscalePlanes) << row.name;
    }
  }
}

// ---------------------------------------------------------------------------
// Base waveform per device.
// ---------------------------------------------------------------------------

// CONTRADICTION ON RECORD — for Diogo to settle on hardware. Do not "fix" it from here.
//
// SleepActivity.cpp says of the grayscale base: "Must stay HALF: the gray nudge LUT is
// calibrated against the pixel state the single-pass HALF waveform leaves behind. A FULL
// (GC) base parks pixels in a different charge state and the differential nudge then
// lands unevenly (blotchy noise in gray areas)." SleepGrayscaleBase.h then returns
// FULL_REFRESH for exactly that board, and its own prose ("The X4 and X3 keep HALF")
// disagrees with its code as well. SleepActivity.cpp separately asserts single-HALF
// "stock parity" for all sleep screens, citing issue #2471's blinking complaint.
//
// The test asserts the CODE, because the code is what ships and what the panel has been
// wearing. Which of the two is right is a question about blotchy grey in a wallpaper on a
// real UC8279 X4, and retained e-ink charge means it cannot be answered from a source
// file.
TEST(SleepFacePaint, TheUc8279NonX3GrayscaleBaseIsFullToday) {
  EXPECT_EQ(planFor(Face::Wallpaper, /*sourceHasGrayscale=*/true, uc8279X4()).base, HalDisplay::FULL_REFRESH);
  EXPECT_EQ(planFor(Face::CoverFallback, /*sourceHasGrayscale=*/true, uc8279X4()).base, HalDisplay::FULL_REFRESH);
}

TEST(SleepFacePaint, TheX3GrayscaleBaseIsHalf) {
  EXPECT_EQ(planFor(Face::Wallpaper, /*sourceHasGrayscale=*/true, uc8279X3()).base, HalDisplay::HALF_REFRESH);
}

TEST(SleepFacePaint, TheX4ProGrayscaleBaseIsHalf) {
  EXPECT_EQ(planFor(Face::Wallpaper, /*sourceHasGrayscale=*/true, ssd1677X4Pro()).base, HalDisplay::HALF_REFRESH);
}

// ---------------------------------------------------------------------------
// Savings found but NOT taken. Each needs a hardware check before anyone acts on it.
// ---------------------------------------------------------------------------

// The "Entering sleep" popup is a full FAST submission that lands BEFORE the face is even
// chosen, so the cheapest lock on the device costs two panel drives to show one picture.
// Composing the popup and the face into a single submission would remove one drive — which
// is precisely the hardware-gated change that must not be made from code inspection. The
// popup exists so a press that then spends seconds reading the card has a visible answer;
// removing it changes what the user sees during that wait, not only what the panel does.
TEST(SleepFacePaint, ThePopupIsCountedSeparatelyAndStillCostsItsOwnSubmission) {
  EXPECT_EQ(sleep_face::POPUP_SUBMISSIONS, 1);
  EXPECT_EQ(totalSubmissions(Face::PlainLector, false, uc8279X4()), 2);
}

// DisplayRefreshPolicy cannot arbitrate anything on this path. Its choose() is gated on
// `requested == Mode::Fast` for all three escalation rules, and every sleep-face
// submission is HALF or FULL, so for the whole sleep path the policy is a no-op that only
// spends budget. Worse, displayGrayBuffer bypasses choose() outright
// (HalDisplay::displayGrayBuffer, noteExternalFastPass) — so the grayscale plane pass, the
// second submission of every tone-carrying face, is a panel drive the policy never sees.
//
// Making the policy see it would change escalation timing and therefore the waveform some
// later page turn runs. Not done here. Recorded so the next reader does not rediscover it.
TEST(SleepFacePaint, EverySleepBaseIsACleanWaveformSoThePolicyNeverEscalatesIt) {
  for (const Row& row : kDevices) {
    for (const Face face : {Face::Wallpaper, Face::CoverFallback, Face::PlainLector}) {
      for (const bool hasTone : {false, true}) {
        const HalDisplay::RefreshMode base = planFor(face, hasTone, row.dev).base;
        EXPECT_NE(base, HalDisplay::FAST_REFRESH) << row.name << " — a FAST base would become policy-escalatable";
      }
    }
  }
}
