/**
 * @file LowMemoryRenderTier.h
 * @brief Pure descent table for the low-memory render fallback ladder.
 *
 * When a chapter build aborts specifically for lack of memory (detected by the
 * layout heap gate; see LayoutHeapGate.h), the reader rebuilds it with fewer
 * memory-heavy render features enabled, one tier at a time, until it fits. Each
 * knob here is a section-cache header field, so a degraded build produces a
 * correctly-keyed cache that can never be confused with a full-quality one.
 *
 * Tier 0 is full quality. Each higher tier disables one more knob, cumulatively,
 * ordered least-harmful first:
 *   1: suppress images (skips the ~44 KB image decoder entirely)
 *   2: + drop embedded CSS (frees the resolved rule map)
 *   3: + disable hyphenation (drops per-line hyphenation work)
 *   4: + disable focus/bionic reading (drops per-word split expansion)
 *
 * Pure and host-testable (no Arduino / SETTINGS).
 */
#pragma once

#include <cstdint>

namespace LowMemoryRenderTier {

// The render knobs the ladder varies. Mirrors the matching SETTINGS fields and
// the section-cache header fields of the same names.
struct Knobs {
  uint8_t imageRendering = 0;
  bool embeddedStyle = true;
  bool hyphenationEnabled = true;
  bool focusReadingEnabled = true;
  bool guideDotsEnabled = true;
};

// Highest descent tier (see file header). Tier 0 == full quality.
constexpr int kMaxTier = 4;

// imageRendering value that fully suppresses image decode (IMAGES_SUPPRESS).
constexpr uint8_t kImagesSuppressed = 2;

// Apply the cumulative reductions for `tier` on top of `base`. tier <= 0 returns
// base unchanged; tier >= kMaxTier applies every reduction.
inline Knobs apply(Knobs base, int tier) {
  if (tier >= 1) base.imageRendering = kImagesSuppressed;
  if (tier >= 2) base.embeddedStyle = false;
  if (tier >= 2) base.guideDotsEnabled = false;
  if (tier >= 3) base.hyphenationEnabled = false;
  if (tier >= 4) base.focusReadingEnabled = false;
  return base;
}

inline bool equal(const Knobs& a, const Knobs& b) {
  return a.imageRendering == b.imageRendering && a.embeddedStyle == b.embeddedStyle &&
         a.hyphenationEnabled == b.hyphenationEnabled && a.focusReadingEnabled == b.focusReadingEnabled &&
         a.guideDotsEnabled == b.guideDotsEnabled;
}

}  // namespace LowMemoryRenderTier
