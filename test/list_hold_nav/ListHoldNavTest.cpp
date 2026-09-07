#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "util/HoldRepeat.h"

// The clamped hold step, lifted out of ButtonNavigator so it can be checked on
// the host: the .cpp pulls in Arduino (millis) and MappedInputManager, neither
// of which exists here. Kept identical to ButtonNavigator::heldIndex, and the
// audit below fails if that stops being true.
namespace {
int heldIndex(const int currentIndex, const int totalItems, const int delta) {
  if (totalItems <= 0) return 0;
  return std::clamp(currentIndex + delta, 0, totalItems - 1);
}

// One press of the nav key, which still wraps (ButtonNavigator::nextIndex).
int nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;
  return (currentIndex + 1) % totalItems;
}
}  // namespace

// --- a single press moves exactly one row -----------------------------------

TEST(ListHoldNav, ASinglePressMovesOneRow) {
  EXPECT_EQ(nextIndex(0, 500), 1);
  EXPECT_EQ(nextIndex(1, 500), 2);
  EXPECT_EQ(nextIndex(248, 500), 249);
}

TEST(ListHoldNav, ASinglePressStillWrapsAtTheEnd) {
  // Long-standing behaviour of the press path, deliberately unchanged: only the
  // HOLD is clamped.
  EXPECT_EQ(nextIndex(499, 500), 0);
}

// --- a hold travels in rows, at a controlled rate ---------------------------

TEST(ListHoldNav, TheFirstRepeatsOfAHoldMoveOneRowEach) {
  // The complaint was a hold jumping a whole page per repeat. The first repeats
  // have to be a row, or a hold cannot be aimed at all.
  int index = 0;
  for (unsigned repeat = 0; repeat < HOLD_REPEAT_COARSE_AFTER; ++repeat) {
    index = heldIndex(index, 500, holdRepeatStep(repeat));
  }
  EXPECT_EQ(index, static_cast<int>(HOLD_REPEAT_COARSE_AFTER));
}

TEST(ListHoldNav, ASustainedHoldGoesCoarseButStaysBounded) {
  // After the ramp each repeat is HOLD_REPEAT_COARSE_STEP rows, not a page.
  const int before = heldIndex(100, 500, holdRepeatStep(HOLD_REPEAT_COARSE_AFTER - 1));
  const int after = heldIndex(100, 500, holdRepeatStep(HOLD_REPEAT_COARSE_AFTER));
  EXPECT_EQ(before, 101);
  EXPECT_EQ(after, 100 + HOLD_REPEAT_COARSE_STEP);
  EXPECT_LE(after - 100, 5) << "a repeat that moves more than five rows is back to blasting through the list";
}

TEST(ListHoldNav, AHoldStopsAtTheLastRowInsteadOfWrapping) {
  // A wrapping hold never ends: the selection runs off the bottom, reappears at
  // the top, and keeps going for as long as the key is down. That is the bug.
  EXPECT_EQ(heldIndex(498, 500, 5), 499);
  EXPECT_EQ(heldIndex(499, 500, 5), 499);
  EXPECT_EQ(heldIndex(499, 500, 1), 499);
}

TEST(ListHoldNav, AHoldStopsAtTheFirstRowInsteadOfWrapping) {
  EXPECT_EQ(heldIndex(3, 500, -5), 0);
  EXPECT_EQ(heldIndex(0, 500, -5), 0);
  EXPECT_EQ(heldIndex(0, 500, -1), 0);
}

TEST(ListHoldNav, AnEmptyListIsNeverIndexed) {
  EXPECT_EQ(heldIndex(0, 0, 1), 0);
  EXPECT_EQ(heldIndex(5, 0, -1), 0);
}

TEST(ListHoldNav, ASingleRowListStaysOnItsOnlyRow) {
  EXPECT_EQ(heldIndex(0, 1, 5), 0);
  EXPECT_EQ(heldIndex(0, 1, -5), 0);
}

// --- the rate itself --------------------------------------------------------

TEST(ListHoldNav, TheHoldRateIsSlowEnoughToRead) {
  // Mirrors ButtonNavigator::LIST_REPEAT_INTERVAL_MS / LIST_REPEAT_START_MS.
  // The audit test below ties these numbers to the header.
  constexpr int kIntervalMs = 200;
  constexpr int kStartMs = 400;
  // Faster than ~120 ms and the panel cannot show the rows going past, which is
  // exactly how a hold turns into a blur; slower than 400 and a long list is a
  // chore.
  EXPECT_GE(kIntervalMs, 120);
  EXPECT_LE(kIntervalMs, 400);
  // The delay before a repeat starts has to be longer than a deliberate press,
  // or single presses get read as the start of a run.
  EXPECT_GE(kStartMs, 300);
}

TEST(ListHoldNav, OneSecondOfHoldingCoversAReadableNumberOfRows) {
  // 5 repeats at 200 ms, all still fine-grained: five rows in the first second.
  // A page-per-repeat hold covered ~70 in the same time.
  int index = 0;
  for (unsigned repeat = 0; repeat < 5; ++repeat) index = heldIndex(index, 500, holdRepeatStep(repeat));
  EXPECT_EQ(index, 5);
  EXPECT_LE(index, 20) << "a second of holding should not cross a whole screen of rows";
}

// --- the constants above are the ones the firmware actually uses ------------
//
// heldIndex and the two rates are restated in this file because ButtonNavigator
// cannot be compiled on the host (Arduino millis, MappedInputManager). Restated
// values rot, so the source is read back and checked.

namespace {
std::string readSource(const char* path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }
}  // namespace

TEST(ListHoldNav, TheRepeatRatesMatchTheHeader) {
  const std::string header = readSource(BUTTON_NAVIGATOR_HEADER);
  EXPECT_TRUE(contains(header, "LIST_REPEAT_INTERVAL_MS = 200"))
      << "the interval this test asserts on is no longer the one ButtonNavigator declares";
  EXPECT_TRUE(contains(header, "LIST_REPEAT_START_MS = 400"))
      << "the start delay this test asserts on is no longer the one ButtonNavigator declares";
}

TEST(ListHoldNav, TheHeldStepIsStillClampedInTheFirmware) {
  const std::string source = readSource(BUTTON_NAVIGATOR_SOURCE);
  EXPECT_TRUE(contains(source, "std::clamp(currentIndex + delta, 0, totalItems - 1)"))
      << "ButtonNavigator::heldIndex no longer clamps; a held key can wrap and run forever again";
}

TEST(ListHoldNav, TheListScreensHoldByRowsNotPages) {
  // The regression this whole file exists for: a hold wired to nextPageIndex
  // moves a screenful per repeat.
  for (const char* path : {LIST_ACTIVITY_SOURCE, STATUS_ACTIVITY_SOURCE}) {
    const std::string source = readSource(path);
    EXPECT_TRUE(contains(source, "heldIndex")) << path << " does not step its hold by rows";
    EXPECT_FALSE(contains(source, "onNextContinuous(\n      [&] { step(ButtonNavigator::nextPageIndex"))
        << path << " still pages on hold";
  }
}

TEST(ListHoldNav, ASwipeStillTravelsAPage) {
  // A body swipe is delivered through the same continuous callback as a hold.
  // Without the split it would inherit the hold's one-row step, and dragging a
  // finger down a long list would move a single row per swipe.
  for (const char* path : {LIST_ACTIVITY_SOURCE, STATUS_ACTIVITY_SOURCE, BROWSER_ACTIVITY_SOURCE}) {
    const std::string source = readSource(path);
    EXPECT_TRUE(contains(source, "swipeDrivenPass()"))
        << path << " gives a swipe the hold's row step instead of a page";
    EXPECT_TRUE(contains(source, "PageIndex")) << path << " has no page jump left for a swipe to use";
  }
}
