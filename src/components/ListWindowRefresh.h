#pragma once

#include <GfxRenderer.h>

#include <cstdint>

#include "ListWindowPolicy.h"

// Runtime glue for windowed list refreshes. BaseTheme::drawList records the
// geometry of the list it draws; an activity that opts in ends its render()
// with present() instead of renderer.displayBuffer(). When two consecutive
// frames differ only by the selected row (see ListWindowPolicy.h), present()
// refreshes just those rows; otherwise it falls back to a normal full-panel
// refresh. Activity::onEnter() invalidates the state so frames from two
// different screens can never window-match each other.
namespace list_window {

// Called by BaseTheme::drawList for every list drawn.
void noteListDraw(int x, int y, int width, int height, int rowHeight, int pageItems, int itemCount, int selectedIndex);

// Forget everything (activity transitions, sleep, anything that repaints the
// panel outside the normal render path).
void invalidate();

// Present the frame rendered since the last present(). extraHash must fold in
// any pixels outside the list that can change while the list geometry stays
// identical (header text, hint labels, current path). Only a FAST_REFRESH
// request is eligible for windowing; HALF/FULL always refresh the full panel.
void present(const GfxRenderer& renderer, HalDisplay::RefreshMode requested = HalDisplay::FAST_REFRESH,
             uint32_t extraHash = 0);

}  // namespace list_window
