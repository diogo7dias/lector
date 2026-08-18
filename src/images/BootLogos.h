#pragma once
#include <cstdint>

// Shared boot-logo table. The image byte arrays live in the generated bootlogoN.h
// headers; they are `static` (internal linkage), so including them in more than one
// translation unit bakes a duplicate copy of every image into flash. This module
// includes them ONCE (BootLogos.cpp) and exposes a single shared table that
// BootActivity, SleepActivity and the unlock banners all reference.
//
// The numbers in the file names are the ones the old lector carried (it shipped 23);
// these six are the ones kept, and the gaps are deliberate — renumbering would only
// break the link back to that history.
//
// Every image is 384x384 and stored PRE-ROTATED 90 degrees counter-clockwise, which is
// what scripts/convert_icon.py emits: GfxRenderer::drawImage rotates the origin but not
// the bits, so an asset drawn on a portrait screen has to arrive already turned.
namespace bootlogos {
constexpr int kLogoSize = 384;

extern const uint8_t* const kAll[];
extern const int kCount;

// The crest to draw, given the index the last sleep screen recorded. Keeps the wake
// showing the same crest the lock screen did, and folds an out-of-range stored index
// (a state file from a build with more logos) back into the table.
const uint8_t* byIndex(uint8_t index);
// A crest picked at random, and the index to record so the wake can match it.
uint8_t randomIndex();
}  // namespace bootlogos
