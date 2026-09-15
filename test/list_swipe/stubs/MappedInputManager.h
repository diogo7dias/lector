#pragma once

#include "ButtonMapping.h"
#include "ListSwipeGesture.h"

inline unsigned long nowMs = 0;
inline unsigned long millis() { return nowMs; }

// Only the hardware boundary is stubbed; the real ButtonNavigator is compiled.
class MappedInputManager {
 public:
  using Button = button_mapping::Button;
  button_mapping::Config config{{0, 1, 2, 3}, 0, 0, true};
  int physicalButton = -1;
  bool pressed = false;
  list_swipe::Scroll scroll = list_swipe::Scroll::None;
  bool released = false;
  bool held = false;
  Button button = Button::NavNext;
  unsigned long heldMs = 0;

  list_swipe::Scroll wasListScrollSwipe() const { return scroll; }
  bool matches(Button queried) const {
    if (physicalButton < 0) return queried == button;
    const auto hardware = button_mapping::resolve(queried, config);
    return hardware.first == physicalButton || hardware.second == physicalButton;
  }
  bool wasPressed(Button queried) const { return pressed && matches(queried); }
  bool wasReleased(Button queried) const { return released && matches(queried); }
  bool isPressed(Button queried) const { return held && matches(queried); }
  unsigned long getHeldTime() const { return heldMs; }
};
