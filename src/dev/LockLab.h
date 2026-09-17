#pragma once

#ifdef LECTOR_LOCK_LAB

#include <cstdint>

#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "dev/LockLabState.h"

class GfxRenderer;

// The Lock Lab: the lock screen and .pxc render recipe the bench settled on.
//
// The on-device bench that found this recipe (a knob per variable, a Home row to reach it,
// a timing report per run) was compiled out of every environment once the answers were in,
// and is now deleted; see git history for it. What stays is the recipe itself, which the
// sleep path uses on builds that define LECTOR_LOCK_LAB. The knobs live on as the
// LockLabState defaults. The pre-clear it chose now ships for every build and every user;
// see activities/boot_sleep/SleepPreClear.h.
namespace locklab {

// The render options the current settings describe.
PxcRenderOptions optionsFor(const LockLabState& state);

}  // namespace locklab

#endif  // LECTOR_LOCK_LAB
