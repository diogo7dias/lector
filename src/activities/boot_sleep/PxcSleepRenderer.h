#pragma once
#include <string>

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
bool renderPxcSleepScreen(GfxRenderer& renderer, const std::string& path);
