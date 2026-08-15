#pragma once

// Pure policy: may a wake keep what the panel is physically holding, and skip the
// boot presentation that would clear it?
//
// It may, and only, when the frame on the glass is the same frame the wake is about
// to restore. Quick Resume is that case: it saves the pre-sleep framebuffer and puts
// it straight back, so retained pixels and restored pixels agree and a differential
// refresh over them is honest.
//
// A custom wallpaper sleep face is NOT that case. The panel holds arbitrary artwork
// and the wake goes on to the home screen or the book, so retained pixels and the
// next paint disagree. Skipping the boot presentation there also skips the
// blank-and-FULL pass that clears the artwork, and a differential waveform only
// drives the pixels that changed — so the wallpaper stays baked into the page.
//
// That failure shipped once before (device photo, 0.15.0) and returned in 0.21.0 via
// upstream #2943, which suppressed the boot screen for every custom sleep face. The
// X3 makes it obvious: begin() refills both controller RAM planes with white, so the
// driver believes the glass is blank while it still shows the wallpaper, and every
// later fast page turn diffs against that wrong baseline.
//
// Math core only: no SD, no framebuffer, no settings object, so it is host-testable.

#include <cstdint>

namespace wake_face {

// What the panel is holding while the device sleeps.
enum class SleepFace : uint8_t {
  QuickResumeFrame,  // the exact pre-sleep framebuffer, restored on wake
  CustomWallpaper,   // arbitrary artwork; the wake paints something else over it
  Other,             // cover, clock, blank, and anything else that is not retained
};

// True when the wake may hand the panel over as-is and suppress the boot presentation.
inline constexpr bool retainsPanelForWake(const SleepFace face) {
  return face == SleepFace::QuickResumeFrame;
}

}  // namespace wake_face
