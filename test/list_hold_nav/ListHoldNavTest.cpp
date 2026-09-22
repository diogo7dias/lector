#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "util/HoldRepeat.h"
#include "util/ListIndex.h"

// The index rules are the PRODUCTION ones: ButtonNavigator::heldIndex and
// friends now forward to list_index, which is pure arithmetic in a header and
// so compiles on the host. This file used to carry its own copy of them plus a
// source-substring audit to catch the copy drifting.
namespace {
int heldIndex(const int currentIndex, const int totalItems, const int delta) {
  return list_index::held(currentIndex, totalItems, delta);
}

int nextIndex(const int currentIndex, const int totalItems) { return list_index::next(currentIndex, totalItems); }
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

// --- page travel, now that it can be called directly ------------------------
//
// A swipe pages; these rules were unreachable from the host before list_index,
// so nothing covered them.

TEST(ListHoldNav, ASwipeMovesAWholePageAndLandsOnItsFirstRow) {
  EXPECT_EQ(list_index::nextPage(0, 500, 10), 10);
  EXPECT_EQ(list_index::nextPage(7, 500, 10), 10) << "a page jump lands on a page boundary, not current+page";
  EXPECT_EQ(list_index::previousPage(25, 500, 10), 10);
}

TEST(ListHoldNav, PagingPastTheEndWrapsToTheStartAndBack) {
  // 500 rows, 10 per page: the last page starts at 490.
  EXPECT_EQ(list_index::nextPage(495, 500, 10), 0);
  EXPECT_EQ(list_index::previousPage(3, 500, 10), 490);
}

TEST(ListHoldNav, AListThatFitsOnOnePageStepsInsteadOfPaging) {
  // No page to jump to, so it degrades to the wrapping single step.
  EXPECT_EQ(list_index::nextPage(0, 5, 10), 1);
  EXPECT_EQ(list_index::nextPage(4, 5, 10), 0);
  EXPECT_EQ(list_index::previousPage(0, 5, 10), 4);
}

TEST(ListHoldNav, PagingAnEmptyOrUnmeasuredListIsNeverIndexed) {
  EXPECT_EQ(list_index::nextPage(0, 0, 10), 0);
  EXPECT_EQ(list_index::previousPage(0, 0, 10), 0);
  // pageRows() can be 0 before the first render measures the viewport.
  EXPECT_EQ(list_index::nextPage(3, 500, 0), 0);
  EXPECT_EQ(list_index::previousPage(3, 500, 0), 0);
}

// --- the rate itself --------------------------------------------------------

TEST(ListHoldNav, TheHoldRateIsSlowEnoughToRead) {
  // Mirrors ButtonNavigator::LIST_REPEAT_INTERVAL_MS / LIST_REPEAT_START_MS.
  // The audit test below ties these numbers to the header.
  constexpr int kIntervalMs = 150;
  constexpr int kStartMs = 300;
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
  // About 5 repeats in the first second (300 ms wait, then 150 ms each), all still
  // fine-grained: five rows.
  // A page-per-repeat hold covered ~70 in the same time.
  int index = 0;
  for (unsigned repeat = 0; repeat < 5; ++repeat) index = heldIndex(index, 500, holdRepeatStep(repeat));
  EXPECT_EQ(index, 5);
  EXPECT_LE(index, 20) << "a second of holding should not cross a whole screen of rows";
}

// --- the wiring the arithmetic cannot see -----------------------------------
//
// The index rules above are exercised directly. What is left to audit is the
// wiring: which rule each screen reaches for, and the two repeat rates, which
// are timing constants rather than arithmetic.

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
  EXPECT_TRUE(contains(header, "LIST_REPEAT_INTERVAL_MS = 150"))
      << "the interval this test asserts on is no longer the one ButtonNavigator declares";
  EXPECT_TRUE(contains(header, "LIST_REPEAT_START_MS = 300"))
      << "the start delay this test asserts on is no longer the one ButtonNavigator declares";
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
