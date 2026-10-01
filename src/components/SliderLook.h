#pragma once

#include <GfxRenderer.h>

#include <algorithm>

// The one look every slider in the firmware wears (the light panel's): a track with a
// 2px frame filled from the left, then boxed minus and plus steppers drawn like the
// in-book menu's open/close sign. Shared so the light panel and the number dialogs
// (margins, sleep timeout, Go to %) cannot drift apart.
namespace slider_look {

constexpr int kRule = 2;
constexpr int kTrackHeight = 22;
// The sign inside a stepper box: an 18px bar with 3px strokes and 5px of air, as on the
// in-book menu's headings (freeink drawAccordionHeader).
constexpr int kSign = 18;
constexpr int kStroke = 3;
constexpr int kPad = 5;
constexpr int kBox = kSign + (kPad + kRule) * 2;

// The track, framed, filled for value out of max. A value past max draws full.
inline void drawTrack(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                      const int value, const int max, const bool ink) {
  renderer.drawRect(x, y, width, height, kRule, ink);
  const int span = std::max(1, max);
  const int fill = width * std::clamp(value, 0, span) / span;
  if (fill > 0) renderer.fillRect(x, y, fill, height, ink);
}

// A boxed minus or plus, centred in the stepper's touch area (which stays larger than
// the box, so it is hit blind).
inline void drawStepper(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                        const bool plus, const bool ink) {
  const int boxX = x + (width - kBox) / 2;
  const int boxY = y + (height - kBox) / 2;
  renderer.drawRect(boxX, boxY, kBox, kBox, kRule, ink);
  const int signX = boxX + kRule + kPad;
  const int signY = boxY + kRule + kPad;
  renderer.fillRect(signX, signY + (kSign - kStroke) / 2, kSign, kStroke, ink);
  if (plus) renderer.fillRect(signX + (kSign - kStroke) / 2, signY, kStroke, kSign, ink);
}

}  // namespace slider_look
