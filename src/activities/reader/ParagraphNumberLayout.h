#pragma once

// Where a paragraph number sits vertically against the line of prose it labels.
//
// Split out from EpubReaderActivity so the rule is exercised by the host tests: the
// activity needs a renderer, a page and an SD card, but this is pure arithmetic and
// the thing most likely to be got subtly wrong.
//
// The trap it exists to avoid: centring the two LINE BOXES. A font's declared ascender
// includes accent room that no letter reaches, and how much differs per face, so matching
// the boxes does not match what the eye sees. Measured against the centre of the body's
// x-height band, that rule left the Double numbers a flat 4px high at every reading size
// and the Small numbers 2px to 3px high and growing with the size — no single constant
// would have fixed both. Centring INK on INK, from the outlines actually baked into each
// face, is exactly centred at every size instead.

/// Vertical placement inputs, all in pixels.
struct ParagraphNumberMetrics {
  int lineTop = 0;         ///< top of the body line's ascender box, in screen coordinates
  int bodyAscender = 0;    ///< body font's declared ascender (baseline = lineTop + this)
  int bodyInkTop = 0;      ///< body 'x' (or 'H') ink height above the baseline; 0 = unknown
  int bodyLineHeight = 0;  ///< body font advanceY, used only by the fallback
  int numAscender = 0;     ///< number font's declared ascender
  int numInkTop = 0;       ///< digit ink height above the number's baseline; 0 = unknown
  int numLineHeight = 0;   ///< number font advanceY, used only by the fallback
};

/// Returns the y to hand to GfxRenderer::drawText, which takes the TOP of the ascender
/// box rather than the baseline. When either ink height is unknown (a face missing both
/// 'x' and 'H', or a glyph that failed to load) this falls back to the old line-box
/// centring, so an exotic font still draws a number in a sane place.
inline int paragraphNumberDrawY(const ParagraphNumberMetrics& m) {
  if (m.bodyInkTop <= 0 || m.numInkTop <= 0) {
    return m.lineTop + (m.bodyLineHeight - m.numLineHeight) / 2;
  }
  const int bodyBaseline = m.lineTop + m.bodyAscender;
  // Put the middle of the digits on the middle of the body's x-height band, then step
  // back up by the number font's ascender to reach the origin drawText expects.
  return bodyBaseline - m.bodyInkTop / 2 + m.numInkTop / 2 - m.numAscender;
}
