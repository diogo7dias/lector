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
// and is now deleted; see git history for it. What stays is the recipe itself and the
// pre-clear it chose, which the sleep path still uses on builds that define
// LECTOR_LOCK_LAB. The knobs live on as the LockLabState defaults.
namespace locklab {

// The render options the current settings describe.
PxcRenderOptions optionsFor(const LockLabState& state);

// Drives every pixel to the opposite rail before a render, as the Pre-clear setting asks,
// and returns what it cost in milliseconds. Zero when it is Off.
uint32_t applyPreClear(GfxRenderer& renderer);

}  // namespace locklab

#endif  // LECTOR_LOCK_LAB
