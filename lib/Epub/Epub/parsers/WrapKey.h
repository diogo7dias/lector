#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// WrapKey: the "wrap-affecting" subset of the section layout cache key.
//
// The full section cache generation hash (Section.cpp `layoutGenerationHash`)
// folds 14 layout-affecting settings. Exactly THREE of those change only the
// vertical PACKING of already-wrapped lines, not the horizontal WRAP (which
// words land on which line): `lineCompression` (line spacing), `viewportHeight`
// (top/bottom margin), and `paragraphSpacing`. The other eleven change the wrap.
//
// `wrapKeyHash` folds only those eleven. Two cache generations with the same
// wrapKeyHash have IDENTICAL wrapped lines and differ only in vertical packing,
// so one can be re-packed from the other without re-running the (~13s) wrap.
// This is the guard the re-pack fast path keys on.
//
// IMPORTANT: `extraParagraphSpacing` is WRAP-affecting (it changes the first-line
// indent, ParsedText.cpp resolveFirstLineIndent) and therefore IS in the eleven.
// Do not move it to the pack set.
//
// The fold mirrors `layoutGenerationHash` field-for-field (FNV-1a over each POD's
// raw bytes, same basis/prime) MINUS the three pack-only fields, preserving the
// relative order of the eleven it keeps. Pure and dependency-free so it is
// host-testable and identical on device and host.
namespace wrapkey {

inline uint32_t fnvFold(uint32_t h, const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

template <typename T>
inline uint32_t fnvFoldPod(uint32_t h, const T& v) {
  return fnvFold(h, &v, sizeof(v));
}

// The eleven wrap-affecting fields, in their `layoutGenerationHash` order (the
// three pack-only fields lineCompression / viewportHeight / paragraphSpacing are
// omitted).
inline uint32_t wrapKeyHash(int fontId, bool extraParagraphSpacing, uint8_t paragraphAlignment, uint16_t viewportWidth,
                            bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering, bool focusReadingEnabled,
                            bool guideDotsEnabled, int firstLineIndentPx, uint8_t wordSpacing) {
  uint32_t h = 2166136261u;
  h = fnvFoldPod(h, fontId);
  h = fnvFoldPod(h, extraParagraphSpacing);
  h = fnvFoldPod(h, paragraphAlignment);
  h = fnvFoldPod(h, viewportWidth);
  h = fnvFoldPod(h, hyphenationEnabled);
  h = fnvFoldPod(h, embeddedStyle);
  h = fnvFoldPod(h, imageRendering);
  h = fnvFoldPod(h, focusReadingEnabled);
  h = fnvFoldPod(h, guideDotsEnabled);
  h = fnvFoldPod(h, firstLineIndentPx);
  h = fnvFoldPod(h, wordSpacing);
  return h;
}

}  // namespace wrapkey
