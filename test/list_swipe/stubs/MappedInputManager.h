#pragma once

#include "ListSwipeGesture.h"

inline unsigned long nowMs = 0;
inline unsigned long millis() { return nowMs; }

// Only the hardware boundary is stubbed; the real ButtonNavigator is compiled.
class MappedInputManager {
 public:
  enum class Button { NavNext, NavPrevious };
  list_swipe::Scroll scroll = list_swipe::Scroll::None;
  bool released = false;
  bool held = false;
  Button button = Button::NavNext;
  unsigned long heldMs = 0;

  list_swipe::Scroll wasListScrollSwipe() const { return scroll; }
  bool wasPressed(Button) const { return false; }
  bool wasReleased(Button queried) const { return released && queried == button; }
  bool isPressed(Button queried) const { return held && queried == button; }
  unsigned long getHeldTime() const { return heldMs; }
};
