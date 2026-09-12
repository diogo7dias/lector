// Source audit: the on-screen keyboard registers a hit rect per key, and something
// has to route contacts against them. It did not. The keys were drawn, their rects
// were recorded, the router was constructed and configured on entry, and no pass
// ever called it — so on a touch reader the keyboard answered the physical buttons
// only, and a tap did nothing.
//
// The three calls below are the whole contract: publish the table the render task
// built, route the loop task's contacts against the published generation, and
// dispatch what comes back.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#ifndef KEYBOARD_TOUCH_SOURCE
#error "KEYBOARD_TOUCH_SOURCE must be defined by the build system"
#endif

namespace {

std::string body() {
  std::ifstream file(KEYBOARD_TOUCH_SOURCE);
  EXPECT_TRUE(file.is_open()) << "cannot open " << KEYBOARD_TOUCH_SOURCE;
  std::stringstream all;
  all << file.rdbuf();
  return all.str();
}

}  // namespace

TEST(KeyboardTouchAudit, ThePaintPublishesItsInteractionTable) {
  const std::string source = body();
  EXPECT_NE(source.find("interactions.beginPublishCycle()"), std::string::npos)
      << "the render task must build into the generation the loop task is not reading";
  EXPECT_NE(source.find("interactions.publish()"), std::string::npos)
      << "a table that is never published is a table the loop task never sees";
}

TEST(KeyboardTouchAudit, ReleasesOwnPhysicalButtonActions) {
  const std::string source = body();
  EXPECT_EQ(source.find("if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {\n    if (upHeld"),
            std::string::npos);
  EXPECT_NE(source.find("if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {\n    if (upHeld"),
            std::string::npos);
  EXPECT_NE(source.find("if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {\n    if (confirmHeld"),
            std::string::npos);
  EXPECT_NE(source.find("if (mappedInput.wasReleased(MappedInputManager::Button::Back))"), std::string::npos);
  EXPECT_NE(source.find("touchRouter.update("), std::string::npos);
  EXPECT_NE(source.find("activateValue(result.event.value"), std::string::npos);
}

TEST(KeyboardTouchAudit, RoutesDuringInteractionTableRebuild) {
  const std::string source = body();
  EXPECT_NE(source.find("if (!mappedInput.hasTouch()) return false;"), std::string::npos);
  EXPECT_EQ(source.find("!mappedInput.hasTouch() || !interactionsReady.load()"), std::string::npos);
}
