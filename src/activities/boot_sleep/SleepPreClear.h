#pragma once

#include <DeviceProfile.h>

#include <cstdint>

class GfxRenderer;

// The clean pass that runs before a full-screen sleep face is drawn.
//
// Matches CrossPoint (SleepActivity::onEnter): one white HALF_REFRESH clears the previous
// screen before painting Dark, Light, Custom, Cover, Cover + Custom, or Cover + Transparent.
// Blank (None) skips this pass and runs its own single white HALF_REFRESH.
//
// Declared here rather than in SleepActivity so the submission count is a pure function over
// DeviceProfile and can be host-tested without a panel. See test/sleep_face_paint.
inline constexpr uint8_t sleepPreClearSubmissions(const DeviceProfile&) { return 1; }

// Clears the panel to white with one HALF_REFRESH before a sleep face is drawn. Returns what
// it cost in milliseconds.
uint32_t sleepPreClear(GfxRenderer& renderer, const DeviceProfile& dev);
