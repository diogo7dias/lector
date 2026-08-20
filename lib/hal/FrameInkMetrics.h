#pragma once

#include <cstddef>
#include <cstdint>

// How hard the next refresh is going to hit the panel.
//
// The anti-ghost policy used to count refreshes and nothing else, which makes thirteen
// page turns of body text and thirteen paints of a black dialog over a cover cost exactly
// the same budget. The second case is the one that visibly ghosts. This is the missing
// input: a cheap number, computed from the framebuffer that is about to be pushed, saying
// how much ink this frame is actually going to move.
//
// WHAT IT MEASURES, AND WHY NOT THE OBVIOUS THING
//
// The obvious measure is an XOR against the previous frame. There is no previous frame:
// the build runs in single-buffer mode, and a shadow copy costs another 48-52 KB out of
// ~380 KB on a device that already has to lend the framebuffer away to build a chapter.
//
// So the frame is reduced to a per-band signature instead — 8 rows per band, a black-pixel
// count and a 32-bit hash each — and only the signature is retained. For an X3 that is 66
// bands, 396 bytes in total. Two derived numbers come out of comparing one frame's
// signature with the last:
//
//   changedBands — bands whose hash differs. A true dirty-region answer, not a guess from
//                  drawing-call bookkeeping, which can drift from what was really drawn.
//   inkChurn     — the summed change in black-pixel count across those bands.
//
// inkChurn under-reports when ink merely MOVES (text reflowing between pages: pixels
// change everywhere, the totals barely move) and over-reports on inversions and image
// swaps (the totals move enormously). Those are precisely the low-ghost and high-ghost
// cases, so the error runs in the safe direction, which is why an imperfect measure is
// worth having here.
//
// Cost: one pass over the framebuffer, a byte-LUT popcount and an FNV-1a hash per byte.
// About 2 ms for 52 KB on a 160 MHz RISC-V core, against a 435 ms X3 fast refresh.
class FrameInkMetrics {
 public:
  // 528 rows (X3) / 8 = 66. Sized for the tallest panel this firmware builds for, with
  // room to spare; a taller panel silently falls back to the whole frame (see update).
  static constexpr uint8_t BAND_ROWS = 8;
  static constexpr uint8_t MAX_BANDS = 80;

  // The scale every score is expressed on. 1000 is a whole-frame inversion: every pixel
  // driven, the worst a single pass can do.
  static constexpr uint16_t MAX_SCORE = 1000;

  struct Result {
    uint16_t score = MAX_SCORE;    // 0..MAX_SCORE, what this frame will cost the panel
    uint8_t changedBands = 0;      // bands whose content differs from the last frame
    uint8_t totalBands = 0;        // bands the frame was divided into
    uint8_t firstChangedBand = 0;  // inclusive; == totalBands when nothing changed
    uint8_t lastChangedBand = 0;   // inclusive
  };

  // Reads the framebuffer, scores it against the previous call, and keeps this frame's
  // signature for the next one. `widthBytes` is the panel's row stride, `height` its row
  // count. A null buffer, a zero geometry, or a panel taller than MAX_BANDS * BAND_ROWS
  // returns the worst-case score, because a frame that cannot be measured must not be
  // assumed cheap.
  Result update(const uint8_t* frameBuffer, uint16_t widthBytes, uint16_t height);

  // Forget the retained signature. Call whenever the panel's contents stop matching what
  // was last measured — after a full clear, or on the far side of a sleep. The next
  // update() then scores worst-case rather than comparing against a frame that is no
  // longer on the glass.
  void reset();

 private:
  uint32_t prevHash_[MAX_BANDS] = {};
  uint16_t prevInk_[MAX_BANDS] = {};
  uint8_t prevBands_ = 0;
  bool primed_ = false;
};
