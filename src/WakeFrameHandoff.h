#pragma once

#include <cstddef>
#include <cstdint>

// One-shot handoff between the wake path in main.cpp and the reader's first
// page paint. When a reader-resume wake restores the saved sleep frame and
// displays it, main arms this with the restored frame's hash. The reader then
// renders the resume page into the framebuffer as usual and asks
// consumeIfMatch(): if the rendered buffer hashes identically to what the
// panel already shows, the reader skips its own (expensive) first refresh —
// the user has been looking at the correct page since the restore. Any
// mismatch (e.g. a battery digit changed in the status bar) falls back to the
// normal refresh, so correctness never depends on the skip.
namespace wake_frame {

// FNV-1a over the framebuffer. Stable across boots; only compared within one.
uint32_t hashBuffer(const uint8_t* data, size_t len);

// Arm the one-shot with the hash of the frame currently on the panel.
void arm(uint32_t restoredHash);

bool isArmed();

// Disarm without consuming (reader took a non-guarded display path).
void disarm();

// One-shot: always disarms. Returns true when armed and the buffer hash
// equals the restored frame's hash — caller may skip its panel refresh.
bool consumeIfMatch(const uint8_t* data, size_t len);

}  // namespace wake_frame
