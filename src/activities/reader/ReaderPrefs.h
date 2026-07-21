#pragma once
#include <cstdint>

#include "Serialization.h"

// Per-book snapshot of the reader-tab layout settings.
//
// A book flagged "custom" (see EpubReaderActivity) carries its own copy of these
// values, fully decoupled from the global CrossPointSettings: changing the global
// reader settings no longer affects it. A normal (non-custom) book uses
// ReaderPrefs::fromGlobal() — a snapshot of the current global values taken when
// the book is opened. The reader reads layout settings exclusively through the
// resolved ReaderPrefs, so the global settings singleton is never mutated.
//
// Persisted per book at <cachePath>/reader_override.bin as [version][POD blob].
// POD only (all uint8_t + one fixed char array) so it round-trips through
// serialization::writePod as one trivially-copyable blob with no alignment
// hazard on the ESP32-C3 — every member is byte-aligned.
//
// The field set mirrors the CrossPointSettings reader-tab fields; keep both in
// sync. Adding/removing a field requires bumping VERSION (old sidecars then fall
// back to the global snapshot, forcing a one-time re-index).
struct ReaderPrefs {
  static constexpr uint8_t VERSION = 1;

  // Font
  uint8_t fontFamily = 0;  // CrossPointSettings::BOOKERLY
  uint8_t fontSize = 0;    // CrossPointSettings::SIZE_14 by default; overwritten by fromGlobal()
  // Spacing / alignment
  uint8_t lineSpacingPercent = 100;
  uint8_t paragraphAlignment = 0;  // JUSTIFIED
  uint8_t wordSpacing = 3;         // WORD_SPACING_ZERO (== 0%)
  uint8_t paragraphSpacing = 0;
  uint8_t extraParagraphSpacing = 1;
  // Margins
  uint8_t uniformMargins = 1;
  uint8_t screenMargin = 5;
  uint8_t screenMarginTop = 5;
  uint8_t screenMarginBottom = 5;
  // First-line indent
  uint8_t firstLineIndentMode = 0;  // FIRST_LINE_INDENT_BOOK
  uint8_t firstLineIndentPercent = 0;
  // Toggles
  uint8_t hyphenationEnabled = 0;
  uint8_t embeddedStyle = 0;
  uint8_t focusReadingEnabled = 0;
  uint8_t guideDotsEnabled = 0;
  uint8_t imageRendering = 0;
  // Orientation
  uint8_t orientation = 0;
  // SD font family name (empty = built-in). Fixed width so the struct stays POD.
  char sdFontFamilyName[32] = "";

  // Capture the current global reader values. Defined in ReaderPrefs.cpp (needs
  // the CrossPointSettings singleton).
  static ReaderPrefs fromGlobal();
};

// Reader font id for these prefs (mirrors CrossPointSettings::getReaderFontId via
// the shared SD-font resolver). Defined in ReaderPrefs.cpp.
int readerFontId(const ReaderPrefs& p);

// Line-height multiplier from lineSpacingPercent. Mirrors
// CrossPointSettings::getReaderLineCompression (bounds 35..150). Pure — host testable.
inline float readerLineCompression(const ReaderPrefs& p) {
  uint8_t pct = p.lineSpacingPercent;
  if (pct < 35) pct = 35;    // CrossPointSettings::MIN_LINE_SPACING_PERCENT
  if (pct > 150) pct = 150;  // CrossPointSettings::MAX_LINE_SPACING_PERCENT
  return static_cast<float>(pct) / 100.0f;
}

// First-line indent in px, or -1 to use the publisher/CSS indent. Mirrors
// firstLineIndentPxFor. Pure — host testable. (mode 1 == FIRST_LINE_INDENT_PERCENT)
inline int readerFirstLineIndentPx(const ReaderPrefs& p, int viewportWidth) {
  if (p.firstLineIndentMode != 1) return -1;
  return viewportWidth * p.firstLineIndentPercent / 200;
}

// ── Serialization: [uint8 version][POD blob] ────────────────────────────────
// Templated forms cover std::ostream / std::istream (host tests); concrete
// HalFile overloads use the checked try*Pod helpers so a truncated/corrupt
// sidecar fails cleanly instead of yielding garbage prefs.

template <typename Out>
void writeReaderPrefs(Out& out, const ReaderPrefs& p) {
  const uint8_t ver = ReaderPrefs::VERSION;
  serialization::writePod(out, ver);
  serialization::writePod(out, p);
}

template <typename In>
bool readReaderPrefs(In& in, ReaderPrefs& p) {
  uint8_t ver = 0;
  serialization::readPod(in, ver);
  if (ver != ReaderPrefs::VERSION) return false;
  serialization::readPod(in, p);
  return true;
}

inline bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p) {
  const uint8_t ver = ReaderPrefs::VERSION;
  return serialization::tryWritePod(out, ver) && serialization::tryWritePod(out, p);
}

inline bool readReaderPrefs(HalFile& in, ReaderPrefs& p) {
  uint8_t ver = 0;
  if (!serialization::tryReadPod(in, ver)) return false;
  if (ver != ReaderPrefs::VERSION) return false;
  return serialization::tryReadPod(in, p);
}
