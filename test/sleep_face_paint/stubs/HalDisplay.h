#pragma once
#include <cstdint>

// Host stub: just the refresh-mode enum, because that is the whole of HalDisplay the
// sleep-face plan depends on. The real header pulls in Arduino, the SDK's EInkDisplay
// and the panel driver, none of which builds on a host.
//
// Values and order must track lib/hal/HalDisplay.h.
class HalDisplay {
 public:
  enum RefreshMode {
    FULL_REFRESH,
    HALF_REFRESH,
    FAST_REFRESH,
  };
};
