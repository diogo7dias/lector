#pragma once

// Whether a list shows its selected row filled black. A keys-only board always does: its
// keys act on that row. A touch board shows it only once a key has been pressed on the
// current screen, because a black row means nothing to a finger, and a tap opens what it
// lands on. MappedInputManager::update() latches the key press; Activity::onEnter() drops
// it, so every screen arrives clean.
namespace key_focus {

inline bool& keyPressedHere() {
  static bool pressed = false;
  return pressed;
}

inline bool showsSelection(const bool hasTouch) { return !hasTouch || keyPressedHere(); }

}  // namespace key_focus
