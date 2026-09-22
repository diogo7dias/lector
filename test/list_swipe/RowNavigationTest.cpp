#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "activities/settings/SettingsListNav.h"
#include "components/SettingsGrid.h"
#include "util/ButtonNavigator.h"
#include "util/HoldRepeat.h"

namespace {
using Button = MappedInputManager::Button;

struct Navigation {
  MappedInputManager input;
  ButtonNavigator nav{ButtonNavigator::LIST_REPEAT_INTERVAL_MS, ButtonNavigator::LIST_REPEAT_START_MS};
  int selected = 10;
  int callbacks = 0;
  bool release = false;
  bool grid = false;
  int count = 40;

  explicit Navigation(bool onRelease = false) : release(onRelease) { ButtonNavigator::setMappedInputManager(input); }

  void dispatch() {
    const auto move = [&](int rows) {
      ++callbacks;
      if (grid) {
        selected = settings_grid::step(selected, count, rows, 0, 1);
      } else {
        selected =
            rows > 0 ? ButtonNavigator::nextIndex(selected, count) : ButtonNavigator::previousIndex(selected, count);
      }
    };
    const std::vector<Button> next = {grid ? Button::ScreenDown : Button::NavNext};
    const std::vector<Button> previous = {grid ? Button::ScreenUp : Button::NavPrevious};
    if (release) {
      nav.onRelease(next, [&] { move(1); });
      nav.onRelease(previous, [&] { move(-1); });
    } else {
      nav.onStep(next, [&] { move(1); });
      nav.onStep(previous, [&] { move(-1); });
    }
  }

  void edge(Button button, uint32_t now, bool press, bool up, unsigned long heldMs = 0) {
    input.button = button;
    input.pressed = press;
    input.released = up;
    input.held = press && !up;
    input.heldMs = heldMs;
    nowMs = now;
    dispatch();
  }
  void tap(Button button, uint32_t now) {
    edge(button, now, true, false);
    edge(button, now + 30, false, true, 30);
  }
};
}  // namespace

TEST(RowNavigation, FirstTapKeepsItsExistingEdgeWithoutWaiting) {
  for (bool release : {false, true}) {
    Navigation n(release);
    n.edge(Button::NavNext, 1000, true, false);
    EXPECT_EQ(n.selected, release ? 10 : 11);
    n.edge(Button::NavNext, 1030, false, true, 30);
    EXPECT_EQ(n.selected, 11);
    EXPECT_EQ(n.callbacks, 1);
  }
}

TEST(RowNavigation, AQuickSecondTapMovesOneRowNotAJump) {
  for (bool release : {false, true}) {
    for (bool grid : {false, true}) {
      for (bool forward : {false, true}) {
        Navigation n(release);
        n.grid = grid;
        const Button button = grid ? (forward ? Button::ScreenDown : Button::ScreenUp)
                                   : (forward ? Button::NavNext : Button::NavPrevious);
        n.tap(button, 1000);
        n.tap(button, 1100);
        EXPECT_EQ(n.selected, forward ? 12 : 8);
        EXPECT_EQ(n.callbacks, 2);
      }
    }
  }
}

TEST(RowNavigation, TapsWrapOnListsAndClampOnGrids) {
  for (bool grid : {false, true}) {
    Navigation n;
    n.grid = grid;
    n.selected = 39;
    n.tap(grid ? Button::ScreenDown : Button::NavNext, 1000);
    EXPECT_EQ(n.selected, grid ? 39 : 0);
  }
}

TEST(RowNavigation, HoldRepeatsAndItsReleaseAddsNothing) {
  for (bool release : {false, true}) {
    Navigation n(release);
    n.edge(Button::NavNext, 1000, true, false);
    EXPECT_EQ(n.selected, release ? 10 : 11);
    n.input.pressed = false;
    n.input.heldMs = ButtonNavigator::LIST_REPEAT_START_MS + 1;
    nowMs = 1000 + ButtonNavigator::LIST_REPEAT_START_MS + 1;
    n.nav.onNextContinuous(
        [&] { n.selected = ButtonNavigator::heldIndex(n.selected, n.count, holdRepeatStep(n.nav.repeats())); });
    EXPECT_EQ(n.nav.repeats(), 1u);
    const int afterHold = n.selected;
    n.edge(Button::NavNext, nowMs + 50, false, true, 450);
    EXPECT_EQ(n.selected, afterHold);
    EXPECT_EQ(n.nav.repeats(), 0u);
    n.tap(Button::NavNext, nowMs + 100);
    EXPECT_EQ(n.selected, afterHold + 1);
  }
}

TEST(RowNavigation, AHoldRampsToCoarseStepsAndStopsAtTheEnd) {
  Navigation n;
  n.input.button = Button::NavNext;
  n.input.held = true;
  n.input.heldMs = ButtonNavigator::LIST_REPEAT_START_MS + 1;
  nowMs = 1000;
  n.selected = 0;
  for (unsigned i = 0; i < HOLD_REPEAT_COARSE_AFTER + 20; ++i) {
    nowMs += ButtonNavigator::LIST_REPEAT_INTERVAL_MS + 1;
    n.nav.onNextContinuous(
        [&] { n.selected = ButtonNavigator::heldIndex(n.selected, n.count, holdRepeatStep(n.nav.repeats())); });
    if (i + 1 == HOLD_REPEAT_COARSE_AFTER) EXPECT_EQ(n.selected, static_cast<int>(HOLD_REPEAT_COARSE_AFTER));
  }
  EXPECT_EQ(n.selected, n.count - 1);
}

TEST(RowNavigation, SwipeStillMovesAPage) {
  Navigation n;
  n.input.scroll = list_swipe::Scroll::PageDown;
  n.nav.onNextContinuous([&] { n.selected = ButtonNavigator::nextPageIndex(n.selected, 40, 10); });
  EXPECT_EQ(n.selected, 20);
}

TEST(RowNavigation, LogicalDirectionsFollowTheLiveOrientation) {
  for (bool grid : {false, true}) {
    for (uint8_t orientation = 0; orientation < 4; ++orientation) {
      Navigation n;
      n.grid = grid;
      n.input.config.orientation = orientation;
      const Button next = grid ? Button::ScreenDown : Button::NavNext;
      n.input.physicalButton = button_mapping::resolve(next, n.input.config).first;
      n.tap(next, 1000);
      EXPECT_EQ(n.selected, 11);
    }
  }
}

TEST(RowNavigation, ReleaseOnlyAndSamePassEdgesMoveOnce) {
  Navigation n(true);
  n.edge(Button::NavNext, 1000, false, true);
  EXPECT_EQ(n.selected, 11);
  n.edge(Button::NavNext, 1100, true, true);
  EXPECT_EQ(n.selected, 12);
  n.edge(Button::NavNext, 1200, true, true);
  EXPECT_EQ(n.selected, 13);
  EXPECT_EQ(n.callbacks, 3);
}
