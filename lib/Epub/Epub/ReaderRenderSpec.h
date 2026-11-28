#pragma once
#include <cstdint>

// Guide Dots states, stored in ReaderRenderSpec::guideDotsMode.
enum GuideDotsMode : uint8_t {
  GUIDE_DOTS_OFF = 0,      // normal inter-word spaces
  GUIDE_DOTS_VISIBLE = 1,  // widened gap with a middle dot drawn in it
  GUIDE_DOTS_HIDDEN = 2,   // widened gap, no dot drawn ("Hidden Dots")
};

// Embedded Style modes, stored in ReaderPrefs::embeddedStyle and ReaderRenderSpec::embeddedStyle.
enum EmbeddedStyleMode : uint8_t {
  EMBEDDED_STYLE_OFF = 0,        // ignore the book's CSS completely
  EMBEDDED_STYLE_TEXT_ONLY = 1,  // keep inline text styling; drop margins, padding, font sizes, boxes
  EMBEDDED_STYLE_FULL = 2,       // honour all supported book CSS
};

constexpr uint8_t resolveEmbeddedStyle(const uint8_t raw) {
  return raw <= EMBEDDED_STYLE_FULL ? raw : EMBEDDED_STYLE_FULL;
}

// The settings UI carries Guide Dots and its Hidden Dots sub-option as two independent
// toggles; the layout engine wants one state. Hidden Dots is meaningless on its own, so
// a stored "hidden" with dots off collapses back to off.
constexpr uint8_t resolveGuideDotsMode(const uint8_t dotsEnabled, const uint8_t dotsHidden) {
  if (dotsEnabled == 0) return GUIDE_DOTS_OFF;
  return dotsHidden != 0 ? GUIDE_DOTS_HIDDEN : GUIDE_DOTS_VISIBLE;
}

// Word Spacing range, percent of the font's natural space advance; the settings row and the
// layout engine clamp to it.
inline constexpr uint8_t READER_WORD_SPACING_MIN = 50;
inline constexpr uint8_t READER_WORD_SPACING_MAX = 200;

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via makeRenderSpec() in ReaderPrefs.h, which fills every field:
// the look-derived ones from a ReaderPrefs, the font id and viewport from the
// caller. Taking those as arguments is what keeps a spec from existing in a
// half-filled state — the 0 defaults below are a last-resort backstop (a 0x0
// viewport lays out nothing), not an invitation to omit them.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  // Baseline space-glyph advance, percent (100 = unchanged); justification adds on top.
  uint8_t wordSpacing = 100;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  // EmbeddedStyleMode: Off (0), Text Only (1), or Full (2). Off skips parsing the stylesheet.
  uint8_t embeddedStyle = EMBEDDED_STYLE_FULL;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  // Guide dots: widen the gap between words as a reading aid, with a middle dot
  // (U+00B7) drawn in it (restored old-lector model). Widening the gap changes line
  // breaks and page fill, and the dot itself is baked into the cached blocks, so the
  // mode is part of the cache key. Hidden mode keeps the widened gap and draws no dot.
  //
  // One byte holding three states rather than two bools: Hidden Dots itself adds
  // no header bytes and needs no cache rebuild for books that are not using dots.
  uint8_t guideDotsMode = GUIDE_DOTS_OFF;
  // First-line paragraph indent in spaces of the reading font, 0..5 (0 = none). Part of
  // the cache key.
  uint8_t paragraphIndentSpaces = 2;
  // Character Spacing: whole pixels added between letters, -2..2 (negative tightens). Every
  // word changes width, so part of the cache key; the renderer applies it
  // (GfxRenderer::setCharacterSpacing).
  int8_t characterSpacing = 0;

  // Memberwise: padding is ignored and float equality keeps its usual semantics.
  constexpr bool operator==(const ReaderRenderSpec&) const = default;
};

// Compare decoded section-cache parameters. Version/trailer validation and the
// on-disk byte order remain owned by Section.
constexpr bool sectionCacheMatches(const ReaderRenderSpec& requested, const ReaderRenderSpec& cached) {
  return requested == cached;
}
