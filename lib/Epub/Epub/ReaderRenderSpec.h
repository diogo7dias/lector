#pragma once
#include <cstdint>

// Guide Dots states, stored in ReaderRenderSpec::guideDotsMode.
enum GuideDotsMode : uint8_t {
  GUIDE_DOTS_OFF = 0,      // normal inter-word spaces
  GUIDE_DOTS_VISIBLE = 1,  // widened gap with a middle dot drawn in it
  GUIDE_DOTS_HIDDEN = 2,   // widened gap, no dot drawn ("Hidden Dots")
};

// The settings UI carries Guide Dots and its Hidden Dots sub-option as two independent
// toggles; the layout engine wants one state. Hidden Dots is meaningless on its own, so
// a stored "hidden" with dots off collapses back to off.
constexpr uint8_t resolveGuideDotsMode(const uint8_t dotsEnabled, const uint8_t dotsHidden) {
  if (dotsEnabled == 0) return GUIDE_DOTS_OFF;
  return dotsHidden != 0 ? GUIDE_DOTS_HIDDEN : GUIDE_DOTS_VISIBLE;
}

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  // Extra block gap after each paragraph, as a percentage of the line height (0 = none).
  // Added on top of extraParagraphSpacing. Part of the cache key. Restored (old lector).
  uint8_t paragraphSpacing = 0;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  // Two independent switches. Text = weight, slant, decoration, super/sub, direction and
  // display:none. Layout = alignment, indent, margins, padding and book-set image sizes.
  // Both off means the stylesheet is never even parsed.
  bool embeddedTextStyle = true;
  bool embeddedLayoutStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  // Guide dots: widen the gap between words as a reading aid, with a middle dot
  // (U+00B7) drawn in it (restored old-lector model). Widening the gap changes line
  // breaks and page fill, and the dot itself is baked into the cached blocks, so the
  // mode is part of the cache key. Hidden mode keeps the widened gap and draws no dot.
  //
  // Deliberately one byte holding three states rather than two bools: it keeps the
  // section-file header the size it has been since v37, so adding Hidden Dots costs
  // no format version bump and no cache rebuild for books that are not using dots.
  uint8_t guideDotsMode = GUIDE_DOTS_OFF;
  // First-line paragraph indent (restored old-lector model). mode: 0 = Book (respect
  // the CSS indent), 1 = Custom % of the column width; percent applies in mode 1.
  // Both are part of the cache key.
  uint8_t firstLineIndentMode = 0;
  uint8_t firstLineIndentPercent = 0;
};
