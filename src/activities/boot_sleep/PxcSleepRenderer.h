#pragma once
#include <HalDisplay.h>

#include <functional>
#include <string>

#include "PxcOverlayTiming.h"

class GfxRenderer;

// Renders a full-screen (panel-sized, e.g. 480x800) pre-dithered 2-bits-per-pixel
// .pxc wallpaper to the e-ink framebuffer using the OEM 3-pass grayscale pipeline
// (BW base + LSB plane + MSB plane), mirroring SleepActivity::renderBitmapSleepScreen.
//
// The .pxc format is the same little-endian-header pixel cache used for EPUB
// images (see ImageBlock.cpp): uint16 width, uint16 height, then 2bpp packed
// pixels (4 px/byte, MSB-first, row-major; level 0..3 -> gray 0/85/170/255).
//
// The image is authored at the exact panel size, so it is drawn at the origin
// with no scaling or cropping and must match the screen within +/-1 px. Returns
// false on open / header / size-mismatch / allocation / read failure so the
// caller can fall through to another sleep screen.
//
// extraOverlay defaults to every pass. FinalComposite skips the visible BW base
// and draws only into the two off-screen grayscale planes, so viewer controls
// appear together with the completed image without an extra panel refresh.
//
// drawInfoOverlay gates the bottom-left filename overlay. The sleep
// screen and PXC viewer leave it on; the unlock banner screen passes false so its
// own bottom banner doesn't collide with the filename box.
//
// grayscale=false renders a single 1-bit (black/white) refresh of the wallpaper +
// overlays, skipping the 3-pass grayscale pipeline. The unlock banner screen uses
// this for a faster wake; the sleep screen and PXC viewer keep the full grayscale.
//
// Ghost note: every pass here uses the calibrated differential HALF/graybase
// waveforms. Callers painting over arbitrary prior content (PXC viewer, sleep
// faces) must deep-clean the panel FIRST (blank + FULL_REFRESH) or the old
// content ghosts through; on X3 the graybase waveform is fixed, so a stronger
// base mode cannot be selected here.
bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path,
                          const std::function<void()>& extraOverlay = nullptr, bool drawInfoOverlay = true,
                          bool grayscale = true, PxcOverlayTiming overlayTiming = PxcOverlayTiming::EveryPass);
