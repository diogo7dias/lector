#pragma once

// The X4 panel crops the first few logical rows at the physical top edge.
// Keep top-edge UI content below that area without changing X3 coordinates.
// Applied globally: BaseTheme::drawHeader shifts every screen header down by
// this amount, so all top bars land at the same visible Y on X4 (X3 inset = 0).
// Also used directly by the boot splash and the feedback banner strip.
constexpr int topEdgeInset(const bool deviceIsX4) { return deviceIsX4 ? 9 : 0; }
