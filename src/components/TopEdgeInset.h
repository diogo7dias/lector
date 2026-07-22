#pragma once

// The X4 panel crops the first couple of logical rows at the physical top edge.
// Keep top-edge UI content below that area without changing X3 coordinates.
constexpr int topEdgeInset(const bool deviceIsX4) { return deviceIsX4 ? 2 : 0; }
