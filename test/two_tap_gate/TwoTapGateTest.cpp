// Two-tap confirmation for touch input: the state machine, and a source audit that
// it is applied at the two places taps become actions rather than screen by screen.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "components/TwoTapGate.h"

namespace {

constexpr uint16_t kRow = 1;
constexpr uint16_t kButton = 2;

two_tap::Gate touchGate() {
  two_tap::Gate gate;
  gate.setEnabled(true);
  return gate;
}

std::string readSource(const char* path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

}  // namespace

// --- first tap arms, and runs nothing ---------------------------------------

TEST(TwoTapGate, TheFirstTapOnARowArmsItAndDoesNotAct) {
  auto gate = touchGate();
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  EXPECT_TRUE(gate.armed());
  EXPECT_EQ(gate.armedAction(), kRow);
  EXPECT_EQ(gate.armedValue(), 3);
}

TEST(TwoTapGate, NothingIsArmedBeforeTheFirstTap) {
  const auto gate = touchGate();
  EXPECT_FALSE(gate.armed());
  EXPECT_EQ(gate.armedAction(), two_tap::kNoAction);
}

// --- second tap on the same row acts ----------------------------------------

TEST(TwoTapGate, TheSecondTapOnTheSameRowActs) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Act);
}

TEST(TwoTapGate, ActingDisarmsSoAThirdTapArmsAgain) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Act);
  EXPECT_FALSE(gate.armed());
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
}

// --- a different row moves the arm without acting ---------------------------

TEST(TwoTapGate, ATapOnADifferentRowMovesTheArmAndActsOnNothing) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  EXPECT_EQ(gate.decide(kRow, 4), two_tap::Decision::Arm);
  EXPECT_EQ(gate.armedValue(), 4);
}

TEST(TwoTapGate, OnlyOneControlIsEverArmed) {
  auto gate = touchGate();
  gate.decide(kRow, 3);
  gate.decide(kRow, 4);
  gate.decide(kButton, 0);
  EXPECT_EQ(gate.armedAction(), kButton);
  EXPECT_EQ(gate.armedValue(), 0);
  // The row armed two taps ago is gone, so a tap on it arms rather than acts.
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
}

TEST(TwoTapGate, TheSameValueUnderADifferentActionIsADifferentControl) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 2), two_tap::Decision::Arm);
  EXPECT_EQ(gate.decide(kButton, 2), two_tap::Decision::Arm);
}

// --- every clear condition clears it ----------------------------------------

TEST(TwoTapGate, ClearingDisarmsSoTheNextTapOnTheSameRowOnlyArms) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  gate.clear();
  EXPECT_FALSE(gate.armed());
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
}

TEST(TwoTapGate, ClearingWhileNothingIsArmedIsHarmless) {
  auto gate = touchGate();
  gate.clear();
  gate.clear();
  EXPECT_FALSE(gate.armed());
}

// Scroll, screen change, physical key and tap-on-nothing all route to clear(); the
// audit below pins that each of those callers exists.
TEST(TwoTapGate, EveryClearConditionIsWiredToTheGate) {
  const std::string host = readSource(UI_APP_HOST_SOURCE);
  EXPECT_TRUE(contains(host, "if (input.wasAnyPressed() || input.tappedHintHardware() >= 0) clearTwoTap();"))
      << "a physical key (or the hint band standing in for one) no longer ends the arm";
  EXPECT_TRUE(contains(host, "} else if (!result.event) {")) << "a tap on empty space no longer ends the arm";
  EXPECT_TRUE(contains(host, "twoTapGate.clear();"));

  const std::string input = readSource(MAPPED_INPUT_SOURCE);
  EXPECT_TRUE(contains(input, "if (gpio.wasAnyPressed()) clearRowTapArm();"));
  EXPECT_TRUE(contains(input, "clearRowTapArm();  // a tap on empty space"));
  // Scrolling: the list scroll swipe and the plain swipe both end it.
  EXPECT_GE(input.find("clearRowTapArm();", input.find("MappedInputManager::wasListScrollSwipe")), 0u);
  EXPECT_TRUE(contains(readSource(ACTIVITY_SOURCE), "mappedInput.clearRowTapArm();"))
      << "leaving a screen no longer ends the arm";
}

// --- a board without touch is untouched -------------------------------------

TEST(TwoTapGate, ABoardWithoutTouchActsOnTheFirstTapAndNeverArms) {
  two_tap::Gate gate;  // never enabled
  EXPECT_FALSE(gate.enabled());
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Act);
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Act);
  EXPECT_EQ(gate.decide(kRow, 4), two_tap::Decision::Act);
  EXPECT_FALSE(gate.armed());
}

TEST(TwoTapGate, LosingTouchDropsAnyArm) {
  auto gate = touchGate();
  ASSERT_EQ(gate.decide(kRow, 3), two_tap::Decision::Arm);
  gate.setEnabled(false);
  EXPECT_FALSE(gate.armed());
  EXPECT_EQ(gate.decide(kRow, 3), two_tap::Decision::Act);
}

TEST(TwoTapGate, NoActionIsNotAControlAndNeverArms) {
  auto gate = touchGate();
  EXPECT_EQ(gate.decide(two_tap::kNoAction, 0), two_tap::Decision::Act);
  EXPECT_FALSE(gate.armed());
}

TEST(TwoTapGate, NoRowIsArmedUntilOneIs) {
  two_tap::armedRow() = two_tap::kNoRow;
  EXPECT_EQ(two_tap::armedRow(), -1);
}

// --- one gate per routing point, not one per screen -------------------------

TEST(TwoTapGate, TheGateSitsOnTheSharedFreeInkUiDispatchAndNotInScreens) {
  const std::string host = readSource(UI_APP_HOST_SOURCE);
  // Every screen's handler is registered through the trampoline, so the gate runs
  // before any of them and no screen can opt out by forgetting something.
  EXPECT_TRUE(contains(host, "Base::on(action, &GatedApp::gateTrampoline, this);"));
  EXPECT_TRUE(contains(host, "host_->twoTap().decide(event.action, event.value) == two_tap::Decision::Arm"));
}

TEST(TwoTapGate, DragsAndLongPressesAreNeverArmed) {
  const std::string host = readSource(UI_APP_HOST_SOURCE);
  EXPECT_TRUE(contains(host, "event.dragPermille < 0 && !event.longPress && !host_->twoTapExempt(event.action)"));
}

// Every exemption is stated at the site that registers the action, with its reason.
TEST(TwoTapGate, EveryExemptionIsDeclaredAtItsCallSite) {
  const std::string status = readSource(STATUS_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(status, "exemptFromTwoTap(ACTION_SLIDER);"));
  EXPECT_TRUE(contains(status, "EXEMPT from two-tap confirmation"))
      << "the slider exemption no longer says why it is exempt";

  const std::string clock = readSource(CLOCK_OFFSET_SOURCE);
  EXPECT_TRUE(contains(clock, "exemptFromTwoTap(ACTION_FIELD);"));
  EXPECT_TRUE(contains(clock, "EXEMPT from two-tap confirmation"));
}

// The three surfaces that route their own contacts instead of going through UiAppHost
// are exempt by code path; each says so at the routing function, so the exemption stays
// a decision rather than something nobody noticed.
TEST(TwoTapGate, TheSelfRoutingSurfacesSayWhyTheyAreExempt) {
  for (const char* path : {KEYBOARD_SOURCE, OPTION_POPUP_HEADER, LIGHT_PANEL_HEADER}) {
    EXPECT_TRUE(contains(readSource(path), "EXEMPT from two-tap confirmation")) << path;
  }
}

// --- the armed control is outlined, never filled ----------------------------

TEST(TwoTapGate, TheArmedControlIsDrawnAsAOnePixelOutline) {
  const std::string tokens = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(tokens, "style.borderWidth = 1;"));
  EXPECT_TRUE(contains(tokens, "outlineArmedControl(tokens.listRow.active"));
  EXPECT_TRUE(contains(tokens, "outlineArmedControl(tokens.button.active"));

  // The self-painted row lists share one painter, so they cannot drift from it.
  const std::string theme = readSource(BASE_THEME_SOURCE);
  EXPECT_TRUE(contains(theme, "if (armed) {"));
  EXPECT_TRUE(contains(theme, "renderer.drawRect(rect.x, rect.y, rect.width, rect.height);"));
}
