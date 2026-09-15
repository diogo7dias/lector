#pragma once

#include <DeviceProfile.h>
#include <HalDisplay.h>

#include <cstdint>

#include "SleepGrayscaleBase.h"

// One Module owns "put this face on the panel".
//
// The domain question on this path is which face goes up; the only Interface the display
// offered was "run this waveform", so every sleep call site carried a literal
// (HalDisplay::HALF_REFRESH) and the number of panel submissions a face costs was
// EMERGENT — you had to read three render functions and a bool to count it. The comment
// at SleepActivity::renderSleepScreen claimed every lock was two submissions, which has
// been false for the grayscale faces for as long as they have existed.
//
// planFor() is that table, as a pure function: (face, source grayscale, device) -> the
// base waveform, how many panel submissions the face costs, and whether the two grayscale
// planes run. Pure means host-testable, and test/sleep_face_paint asserts every row.
//
// WHAT THIS MODULE DOES NOT DO: it does not change a waveform and it does not change a
// submission count. A waveform choice and the number of panel drives are physically
// observable on e-ink — wrong choices show as ghosting, blotchy grey or visible blinking —
// and retained charge means neither can be verified by reading code. Every row below
// encodes what the firmware does TODAY. Changing one needs a hardware check first.
//
// Product-locked: exactly three faces exist. Do not add, merge or delete one.
namespace sleep_face {

enum class Face : uint8_t {
  // A .pxc or .bmp from /sleep (or /sleep.bmp, /sleep.pxc at the root). The priority path.
  Wallpaper,
  // The open book's cover, shown when no wallpaper could be. Renders through exactly the
  // same bitmap path as a .bmp wallpaper, which is why it shares its rows.
  CoverFallback,
  // A white page with "Lector" centred. Where every face that fails lands.
  PlainLector,
};

struct PaintPlan {
  // The waveform of the face's FIRST panel submission: the 1-bit paint, or the black-and
  // -white base under the grayscale planes.
  HalDisplay::RefreshMode base = HalDisplay::HALF_REFRESH;
  // Panel submissions this face costs, NOT counting the "Entering sleep" popup that
  // SleepActivity::renderSleepScreen puts up before any face is chosen. See
  // POPUP_SUBMISSIONS below for the total.
  uint8_t submissions = 1;
  // Whether the LSB and MSB grayscale planes run after the base. The plane pass is the
  // second submission whenever it does.
  bool grayscalePlanes = false;
};

// The "Entering sleep" popup, drawn before the face is picked because reading the card
// can take seconds and the press needs a visible answer first. One FAST submission,
// every lock, every face.
inline constexpr uint8_t POPUP_SUBMISSIONS = 1;

// sourceHasGrayscale is what the SOURCE offers and the cover filter allows, not what the
// panel can do: a .bmp whose tone survived the filter (Bitmap::hasGreyscale() &&
// filter == NO_FILTER) takes the 3-pass pipeline, a flattened one does not. A .pxc
// wallpaper always passes true — the 1-bit path is not offered for a lock face, because
// tone is the whole point of the picture the device wears while it is off.
inline constexpr PaintPlan planFor(const Face face, const bool sourceHasGrayscale, const DeviceProfile& dev) {
  PaintPlan plan;
  switch (face) {
    case Face::Wallpaper:
    case Face::CoverFallback:
      if (sourceHasGrayscale) {
        // OEM 3-pass grayscale: a black-and-white base, then the LSB and MSB planes
        // committed together. Two submissions, and the base waveform is the one the
        // gray-nudge LUT was calibrated against on this board.
        plan.base = sleepGrayscaleBaseRefresh(dev);
        plan.submissions = 2;
        plan.grayscalePlanes = true;
        return plan;
      }
      // Flattened to black and white: one clean pass, no planes.
      plan.base = HalDisplay::HALF_REFRESH;
      plan.submissions = 1;
      plan.grayscalePlanes = false;
      return plan;
    case Face::PlainLector:
    default:
      // Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
      // firmware's only clean refresh in normal operation is the single-pass 0xD7
      // sequence, used once for the sleep image. It never runs the multi-flash GC
      // waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
      plan.base = HalDisplay::HALF_REFRESH;
      plan.submissions = 1;
      plan.grayscalePlanes = false;
      return plan;
  }
}

// What one lock actually costs the panel, popup included. The prose this replaces said
// "two panel submissions" for every lock, which was only ever true of the 1-bit faces.
inline constexpr uint8_t totalSubmissions(const Face face, const bool sourceHasGrayscale, const DeviceProfile& dev) {
  return static_cast<uint8_t>(POPUP_SUBMISSIONS + planFor(face, sourceHasGrayscale, dev).submissions);
}

}  // namespace sleep_face
