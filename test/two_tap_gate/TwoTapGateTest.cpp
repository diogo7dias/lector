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
  EXPECT_TRUE(contains(host, "event.dragPermille < 0 && !event.longPress && host_->needsConfirmTap(event)"));
}

// A tap opens. Only a control a screen names as one that cannot be undone arms first,
// so the default answer of the host is no.
TEST(TwoTapGate, OnlyNamedControlsAskForAConfirmingTap) {
  const std::string hostHeader = readSource(UI_APP_HOST_HEADER);
  EXPECT_TRUE(
      contains(hostHeader, "virtual bool needsConfirmTap(const freeink::ui::ActionEvent&) const { return false; }"));
}

// --- the armed control is outlined, never filled ----------------------------

TEST(TwoTapGate, TheArmedControlIsDrawnAsDashedTwoPixelLines) {
  const std::string tokens = readSource(THEME_TOKENS_HEADER);
  EXPECT_TRUE(contains(tokens, "style.borderWidth = 2;"));
  EXPECT_TRUE(contains(tokens, "style.borderDashed = true;"));
  EXPECT_TRUE(contains(tokens, "outlineArmedControl(tokens.listRow.active"));
  EXPECT_TRUE(contains(tokens, "outlineArmedControl(tokens.button.active"));
}
