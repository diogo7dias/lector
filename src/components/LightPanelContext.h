#pragma once

#include <cstdint>

#include "LightPanelGeometry.h"

namespace light_panel {

// What the host puts in the panel for the screen that is currently up. Rebuilt every time
// the panel opens and after every step, so a value shown in the aux row is never stale.
struct Context {
  // The aux row: Text Size in a book, Sort outside one. Empty text means the context has
  // nothing to step, and the row is not drawn at all.
  char auxText[48] = {};
  // Optional second aux row (Filter in Library). Empty means not drawn.
  char aux2Text[48] = {};
  // CrossPointSettings::LONG_PRESS_MENU_FUNCTION values, in grid order.
  uint8_t actions[kMaxActions] = {};
  int actionCount = 0;

  bool hasAux() const { return auxText[0] != '\0'; }
  bool hasAux2() const { return aux2Text[0] != '\0'; }
};

}  // namespace light_panel
