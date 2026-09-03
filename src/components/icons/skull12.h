#pragma once

#include <Icon.h>

#include <cstdint>

// Hand-drawn 12x12 skull for the home header. Not from Lucide: at 12px an SVG
// trace turns to mush, so every pixel here is placed by hand.
//
// The byte order is NOT the picture read left-to-right. GfxRenderer::drawIcon
// plots asset (row, col) at screen (x + size-1-row, y + col), so the asset is
// stored quarter-turned. Bit 1 = leave the pixel, bit 0 = draw black.
//
// The screen-space art these bytes produce, which is what to edit against:
//
//   ...######...      dome
//   .##########.
//   ############
//   ############
//   ##...##...##      eye sockets
//   ##...##...##
//   ##...##...##
//   ############
//   #####..#####      nose
//   .##########.
//   ..########..      jaw
//   ..##.##.##..      teeth
static constexpr uint8_t Skull12IconBits[] = {
    0xC0, 0x7F, 0x80, 0x3F, 0x8E, 0x0F, 0x0E, 0x0F, 0x0E, 0x1F, 0x00, 0x8F,
    0x00, 0x8F, 0x0E, 0x1F, 0x0E, 0x0F, 0x8E, 0x0F, 0x80, 0x3F, 0xC0, 0x7F,
};

// opticalCenterY 6: the artwork fills the box evenly top to bottom, so its centre
// of mass sits on the middle row.
static const freeink::Icon Skull12Icon = {12, 12, 6, Skull12IconBits};
