#pragma once

#include <cstddef>
#include <cstdint>

// LayoutParams — the single value type that both *drives* a chapter layout and
// *identifies* its section cache. Every field here is a layout-affecting render
// input; two LayoutParams that compare equal must produce byte-identical pages,
// and two that differ must land in different cache generations.
//
// Before this type existed, these 14 inputs were spelled out positionally at ~14
// sites (Section's hash/header-write/header-read-compare, the parser ctor, and
// eight reader call sites). Any site that dropped an input or fell out of order
// could serve a stale cached layout as a cache HIT — the root of the recurring
// "font/size change did nothing" bug. Now the field list lives once:
//   - hash()      folds every field  -> the cache generation id
//   - operator==  compares every field (defaulted) -> cache accept/reject
//   - writeFields / readFields (see Section) serialize every field
// Adding a layout setting = add one member here; the golden test in
// test/layout_params guards that hash() actually moves for it.
//
// Field ORDER below is the canonical serialization order and must stay stable
// (a reorder changes the on-disk header layout — bump SECTION_FILE_VERSION).
struct LayoutParams {
  int fontId = 0;                // resolved reader font id (built-in or SD)
  float lineCompression = 1.0f;  // line-height multiplier
  bool extraParagraphSpacing = false;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;  // oriented, margin-inset text width
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = false;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  bool guideDotsEnabled = false;
  int firstLineIndentPx = 0;  // -1 = use publisher/CSS indent
  uint8_t wordSpacing = 0;
  uint8_t paragraphSpacing = 0;

  // Fieldwise comparison (C++20 defaulted). Used by the cache-load path to
  // decide whether an on-disk section matches the requested layout.
  bool operator==(const LayoutParams&) const = default;

  // FNV-1a fold over every field, in declaration order. Pure and deterministic:
  // no HalFile, no SETTINGS, no renderer — fully host-testable. Byte reads are
  // used so there is no RISC-V alignment hazard.
  uint32_t hash() const {
    uint32_t h = 2166136261u;
    auto fold = [&h](const void* data, size_t len) {
      const auto* p = static_cast<const uint8_t*>(data);
      for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
      }
    };
    fold(&fontId, sizeof(fontId));
    fold(&lineCompression, sizeof(lineCompression));
    fold(&extraParagraphSpacing, sizeof(extraParagraphSpacing));
    fold(&paragraphAlignment, sizeof(paragraphAlignment));
    fold(&viewportWidth, sizeof(viewportWidth));
    fold(&viewportHeight, sizeof(viewportHeight));
    fold(&hyphenationEnabled, sizeof(hyphenationEnabled));
    fold(&embeddedStyle, sizeof(embeddedStyle));
    fold(&imageRendering, sizeof(imageRendering));
    fold(&focusReadingEnabled, sizeof(focusReadingEnabled));
    fold(&guideDotsEnabled, sizeof(guideDotsEnabled));
    fold(&firstLineIndentPx, sizeof(firstLineIndentPx));
    fold(&wordSpacing, sizeof(wordSpacing));
    fold(&paragraphSpacing, sizeof(paragraphSpacing));
    return h;
  }
};
