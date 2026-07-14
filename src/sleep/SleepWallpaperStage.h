#pragma once

#include <cstdint>

class GfxRenderer;

// Idle prestager for the sleep wallpaper. At idle it picks the NEXT wallpaper
// through the normal rotation machinery (cursor advance + bookkeeping) and
// converts its 2bpp .pxc payload into the 1bpp framebuffer planes the sleep
// screen displays (PxcPlanePacker), written to a staged SD file. The lock
// path then streams the planes straight into the framebuffer — no decode, no
// per-pixel work — cutting several seconds off every lock. When no valid
// stage exists (first lock after boot, settings changed, conversion aborted),
// the lock falls back to the existing blocking pick+render path unchanged.
//
// Stage file: /.crosspoint/sleep_stage.bin
//   512-byte header (magic, version, quality, format, geometry, image path)
//   BW plane, then (Pretty quality only) LSB and MSB planes, each
//   strideBytes * phyHeight bytes.
// Written to a .tmp and renamed on completion, so a partial conversion can
// never be mistaken for a stage.
namespace crosspoint {
namespace sleep {
namespace stage {

// Advance the prestage a bounded slice (a few ms). Call from the loop() idle
// branch after windex::pumpIdle(); it self-gates until the wallpaper index's
// idle scan has settled so it can never trigger a blocking index build.
// strideBytes/phyHeight come from the renderer (panel geometry).
void pumpIdle(uint16_t strideBytes, uint16_t phyHeight);

// Lock path: if a complete stage matching the current settings and panel
// geometry exists, render it (planes + info overlay + the same refresh
// sequence renderPxcSleepScreen uses) and consume the stage file. Returns
// false — with the panel untouched — when there is no usable stage.
bool renderStaged(GfxRenderer& renderer);

}  // namespace stage
}  // namespace sleep
}  // namespace crosspoint
