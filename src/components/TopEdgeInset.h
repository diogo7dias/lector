#pragma once

// The X4 panel crops the first few logical rows at the physical top edge.
// Keep top-edge UI content below that area without changing X3 coordinates.
constexpr int topEdgeInset(const bool deviceIsX4) { return deviceIsX4 ? 15 : 0; }
