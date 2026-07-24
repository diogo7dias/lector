#pragma once
#include <cstdint>

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
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  // First-line paragraph indent (restored old-lector model). mode: 0 = Book (respect
  // the CSS indent), 1 = Custom % of the column width; percent applies in mode 1.
  // Both are part of the cache key.
  uint8_t firstLineIndentMode = 0;
  uint8_t firstLineIndentPercent = 0;
};
