#pragma once

// The X4 panel crops the first few logical rows at the physical top edge.
// Keep top-edge UI content below that area without changing X3 coordinates.
//
// Applied globally through the two top-origin systems, so every screen shifts
// down uniformly on X4 (X3 inset = 0) and nothing collides or clips:
//   - Chrome/menus/lists/settings all derive their top from metrics.topPadding,
//     so UITheme folds this inset into topPadding (see chromeTopPadding).
//   - The reader page + reader status bar derive their top from
//     GfxRenderer::getOrientedViewableTRBL, which adds this inset (injected via
//     setTopEdgeInset) to the physical-top margin so it follows orientation.
// The boot splash and the feedback banner strip fill black from y=0 and inset
// only their text, so they keep using topEdgeInset() directly.
constexpr int topEdgeInset(const bool deviceIsX4) { return deviceIsX4 ? 9 : 0; }

// The chrome top origin (metrics.topPadding) with the X4 crop folded in. Pure —
// host-testable — so the "X4 == X3 shifted down by the inset" invariant is guarded.
constexpr int chromeTopPadding(const int baseTopPadding, const bool deviceIsX4) {
  return baseTopPadding + topEdgeInset(deviceIsX4);
}
