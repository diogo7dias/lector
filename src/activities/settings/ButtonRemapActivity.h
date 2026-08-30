#pragma once

#include <functional>
#include <string>

#include <vector>

#include "activities/UiListActivity.h"

// Assigns the four front buttons one role at a time: whichever button is pressed
// takes the role the list is standing on. Every button belongs to the remap while
// it is up, so this screen consumes the whole input pass and the base's own
// navigation never runs.
class ButtonRemapActivity final : public UiListActivity {
 public:
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ButtonRemap", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override {}
  bool handleCustomInput() override;
  ListChrome chrome() const override;

 private:
  std::vector<freeink::ui::ListItem> rows;
  // Rendering task state.

  // Index of the logical role currently awaiting input.
  uint8_t currentStep = 0;
  // Temporary mapping from logical role -> hardware button index.
  uint8_t tempMapping[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  // Error banner timing (used when reassigning duplicate buttons).
  unsigned long errorUntil = 0;
  std::string errorMessage;

  // Commit temporary mapping to settings.
  void applyTempMapping();
  // Returns false if a hardware button is already assigned to a different role.
  bool validateUnassigned(uint8_t pressedButton);
  // Labels for UI display.
  const char* getRoleName(uint8_t roleIndex) const;
  const char* getHardwareName(uint8_t buttonIndex) const;
};
