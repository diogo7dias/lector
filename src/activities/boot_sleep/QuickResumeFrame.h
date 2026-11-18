#pragma once

// Quick Resume keeps the page on the panel while the device sleeps, so the wake has to put
// the same page back in the framebuffer: deep sleep is a chip reset and the RAM is gone.
// The frame (the page with the moon stamped on it) is one 48 KB file on the card.
class GfxRenderer;

namespace quick_resume {

inline constexpr char FRAME_PATH[] = "/.crosspoint/sleep_frame.bin";

// Writes the renderer's framebuffer under FRAME_PATH, `.part` first and renamed into place
// once whole, so a lock cut short never leaves a half frame for the wake to trust.
bool saveFrame(const GfxRenderer& renderer);

// Reads FRAME_PATH into the display framebuffer and deletes it: a frame is good for one
// wake. False when there is no frame or it is short; the caller then wakes like any other
// painted face.
bool restoreFrame();

}  // namespace quick_resume
