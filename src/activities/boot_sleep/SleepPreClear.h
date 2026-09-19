#pragma once

#include <DeviceProfile.h>

#include <cstdint>

class GfxRenderer;

// Conditioning before the CUSTOM wallpaper path (including its cover/plain fallbacks).
// X3 requestResync(1) forces a white DTM1 baseline, not a separate white target:
// Uc8253X3Driver::displayStart writes the image into DTM2. The full bank's WW and WB
// cells differ, so this does not establish a uniform black/white clearing cycle.
// An extra all-white FULL selects WW everywhere. Whether it removes text ghosting
// or horizontal banding, and preserves wallpaper tones, still needs a device check.
//
// One renderer call on X3, but TWO panel refresh commands in the warm lock path:
// the full sync and displayFinish's FAST settle. The existing grayscale base still
// runs its resync, conditioning, FAST settle and preBwMid refresh afterwards.
// Other boards retain four FULL renderer calls (black/white twice).
// Returns elapsed milliseconds, including the additional visible lock flash.
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev);
