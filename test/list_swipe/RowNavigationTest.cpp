#include <gtest/gtest.h>

#include <cstdint>

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
        selected = rows > 0 ? ButtonNavigator::nextIndex(selected, count, rows)
                            : ButtonNavigator::previousIndex(selected, count, -rows);
      }
    };
    nav.onRowTap(grid ? Button::ScreenDown : Button::NavNext, [&](int rows) { move(rows); }, release);
    nav.onRowTap(grid ? Button::ScreenUp : Button::NavPrevious, [&](int rows) { move(-rows); }, release);
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

TEST(RowNavigation, DoubleTapMovesExactlyFiveInEitherDirectionOnEitherEdge) {
  for (bool release : {false, true}) {
    for (Button button : {Button::NavNext, Button::NavPrevious}) {
      Navigation n(release);
      n.tap(button, 1000);
      n.tap(button, 1200);
      EXPECT_EQ(n.selected, button == Button::NavNext ? 15 : 5);
      EXPECT_EQ(n.callbacks, 2);  // one final selection/render per tap
    }
  }
}

TEST(RowNavigation, WindowIsStrictly280MsFromReleaseToNextPress) {
  for (uint32_t gap : {279, 280, 281}) {
    Navigation n;
    n.tap(Button::NavNext, 1000);
    n.tap(Button::NavNext, 1030 + gap);
    EXPECT_EQ(n.selected, gap < button_gestures::DOUBLE_WINDOW_MS ? 15 : 12);
  }
}

TEST(RowNavigation, ThirdTapStartsANewPairAndDirectionChangesCancelThePair) {
  Navigation n;
  n.tap(Button::NavNext, 1000);
  n.tap(Button::NavNext, 1100);
  n.tap(Button::NavNext, 1200);
  EXPECT_EQ(n.selected, 16);
  n.tap(Button::NavPrevious, 1300);
  n.tap(Button::NavNext, 1400);
  EXPECT_EQ(n.selected, 16);
}

TEST(RowNavigation, WindowSurvivesMillisRollover) {
  Navigation n;
  n.tap(Button::NavNext, UINT32_MAX - 60);
  n.tap(Button::NavNext, 20);
  EXPECT_EQ(n.selected, 15);
}

TEST(RowNavigation, WrappingAndClampingStillUseTheOriginalMovementPolicy) {
  for (bool grid : {false, true}) {
    for (bool forward : {false, true}) {
      Navigation n;
      n.grid = grid;
      n.selected = forward ? 38 : 1;
      const Button button =
          grid ? (forward ? Button::ScreenDown : Button::ScreenUp) : (forward ? Button::NavNext : Button::NavPrevious);
      n.tap(button, 1000);
      n.tap(button, 1150);
      EXPECT_EQ(n.selected, grid ? (forward ? 39 : 0) : (forward ? 3 : 36));
    }
  }
  for (int count : {0, 1, 2, 3}) {
    EXPECT_EQ(ButtonNavigator::nextIndex(0, count, 5), count > 0 ? 5 % count : 0);
    EXPECT_EQ(ButtonNavigator::previousIndex(0, count, 5), count > 0 ? (count - 5 % count) % count : 0);
  }
}

TEST(RowNavigation, HoldStillRepeatsAndItsReleaseCannotSeedADoubleTap) {
  for (bool release : {false, true}) {
    Navigation n(release);
    n.edge(Button::NavNext, 1000, true, false);
    EXPECT_EQ(n.selected, release ? 10 : 11);
    n.input.pressed = false;
    n.input.heldMs = 401;
    nowMs = 1401;
    n.nav.onNextContinuous(
        [&] { n.selected = ButtonNavigator::heldIndex(n.selected, n.count, holdRepeatStep(n.nav.repeats())); });
    EXPECT_EQ(n.nav.repeats(), 1u);
    const int afterHold = n.selected;
    n.edge(Button::NavNext, 1450, false, true, 450);
    EXPECT_EQ(n.selected, afterHold);
    EXPECT_EQ(n.nav.repeats(), 0u);
    n.tap(Button::NavNext, 1500);
    EXPECT_EQ(n.selected, afterHold + 1);
  }
}

TEST(RowNavigation, TapThenHoldCanDoubleButHoldingDoesNotSeedAnotherPair) {
  Navigation n;
  n.tap(Button::NavNext, 1000);
  n.edge(Button::NavNext, 1100, true, false);
  EXPECT_EQ(n.selected, 15);
  n.edge(Button::NavNext, 1700, false, true, 600);
  n.tap(Button::NavNext, 1800);
  EXPECT_EQ(n.selected, 16);
}

TEST(RowNavigation, SwipeAndOtherControlsCancelThePairWithoutAnExtraRow) {
  Navigation n;
  n.tap(Button::NavNext, 1000);
  n.input.scroll = list_swipe::Scroll::PageDown;
  n.input.released = false;
  n.nav.onNextContinuous([&] { n.selected = ButtonNavigator::nextPageIndex(n.selected, 40, 10); });
  EXPECT_EQ(n.selected, 20);
  n.input.scroll = list_swipe::Scroll::None;
  n.tap(Button::NavNext, 1100);
  EXPECT_EQ(n.selected, 21);
  n.nav.resetRowTap();
  n.tap(Button::NavNext, 1200);
  EXPECT_EQ(n.selected, 22);
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
      n.tap(next, 1100);
      EXPECT_EQ(n.selected, 15);
    }
  }
}

TEST(RowNavigation, NonRowPressAndReleaseUsersNeverAccelerate) {
  Navigation n;
  for (uint32_t time : {1000, 1100}) {
    n.input.pressed = true;
    n.input.released = false;
    nowMs = time;
    n.nav.onNextStep([&] { ++n.selected; });
    n.input.pressed = false;
    n.input.released = true;
    n.nav.onNextStep([&] { ++n.selected; });
  }
  EXPECT_EQ(n.selected, 12);
}

TEST(RowNavigation, HeadingAwareListsCountContentRowsAndSkipHeaders) {
  const std::vector<bool> headers = {true, false, false, true, false, true, false, false, true, false};
  for (bool forward : {false, true}) {
    Navigation n(true);
    int selected = forward ? 1 : 9;
    const auto button = forward ? Button::NavNext : Button::NavPrevious;
    const auto dispatch = [&] {
      n.nav.onRowTap(
          button,
          [&](int rows) {
            for (int row = 0; row < rows; ++row) selected = settings_nav::nextRow(selected, headers, forward);
          },
          /*onRelease=*/true);
    };
    for (uint32_t time : {1000, 1100}) {
      nowMs = time;
      n.input.button = button;
      n.input.pressed = true;
      n.input.released = false;
      dispatch();
      nowMs += 30;
      n.input.pressed = false;
      n.input.released = true;
      dispatch();
    }
    EXPECT_EQ(selected, forward ? 9 : 1);
    EXPECT_FALSE(headers[selected]);
  }
}

TEST(RowNavigation, ReleaseOnlyAndSamePassHintEdgesStillMoveOnce) {
  Navigation n(true);
  n.edge(Button::NavNext, 1000, false, true);
  EXPECT_EQ(n.selected, 11);
  n.nav.resetRowTap();
  n.edge(Button::NavNext, 1100, true, true);
  EXPECT_EQ(n.selected, 12);
  n.edge(Button::NavNext, 1200, true, true);
  EXPECT_EQ(n.selected, 16);
  EXPECT_EQ(n.callbacks, 3);
}
