#pragma once
#include <cstdint>
#include <istream>
#include <ostream>

// Per-book snapshot of the reader "look" settings that the in-book Reader
// Settings screen can change.
//
// Model: the reader-tab settings are GLOBAL by default. A book with no override
// reads ReaderPrefs::fromGlobal() and keeps following the global settings. The
// moment the user changes anything from the in-book Reader Settings screen the
// whole book freezes as "custom" (a full snapshot decoupled from global) and is
// persisted at <cachePath>/reader_override.bin as [version][POD blob]. A "Reset
// Reader Settings" row deletes that file and the book follows global again.
//
// The reader lays out exclusively through the resolved ReaderPrefs (never the
// global singleton), so a custom book never disturbs global state and global
// changes never touch a custom book. Because CrossPoint's section cache keys on
// the ReaderRenderSpec these fields feed, a per-book change invalidates and
// rebuilds only that book's cache automatically — the indexing is unchanged.
//
// Fields map 1:1 onto the fields TextSettingsActivity edits, plus paragraphNumbering
// (an in-menu per-book toggle, wired later). The struct is trivially-copyable POD
// (all uint8_t + one fixed char[32], no padding) so change-detection is a plain
// memcmp and the blob is safe to read back on the RISC-V target (no unaligned loads).
struct ReaderPrefs {
  // Bump whenever the field set changes: readReaderPrefs rejects a mismatched
  // version, so an old sidecar is ignored and the book falls back to global.
  static constexpr uint8_t VERSION = 5;  // v5: first-line indent Book/Custom% (was space-widths)

  // Font (Family/Size tabs)
  uint8_t fontFamily = 0;  // CrossPointSettings::VOLLKORN
  uint8_t fontSize = 1;    // CrossPointSettings::MEDIUM
  // Layout tab
  uint8_t lineSpacingPercent = 100;  // % of natural line height (restored granular)
  uint8_t paragraphAlignment = 0;    // CrossPointSettings::JUSTIFIED
  uint8_t extraParagraphSpacing = 1;
  uint8_t paragraphSpacing = 0;  // % of line height (block gap; restored granular)
  uint8_t screenMargin = 5;      // horizontal (left/right), also all sides when uniformMargins
  uint8_t screenMarginTop = 5;
  uint8_t screenMarginBottom = 5;
  uint8_t uniformMargins = 1;  // 1 = all sides use screenMargin; 0 = separate H / Top / Bottom
  uint8_t dynamicMargins = 0;  // 0 = off, 1 = auto (min 10px), 2 = auto (min 20px)
  // Style tab
  uint8_t focusReadingEnabled = 0;
  uint8_t guideDotsEnabled = 0;  // middle dot between words (restored)
  uint8_t hyphenationEnabled = 0;
  uint8_t embeddedStyle = 1;
  uint8_t textAntiAliasing = 1;
  // Fed into the render spec (edited from the Reader settings category, snapshotted here).
  uint8_t imageRendering = 0;  // CrossPointSettings::IMAGES_DISPLAY
  // Paragraph numbering — per-book, chosen from the in-book menu (off by default).
  // 0 = off, 1 = per chapter, 2 = whole book.
  uint8_t paragraphNumbering = 0;  // CrossPointSettings::PARA_NUM_OFF
  // Paperback Look (heavier ink smear) — per book, toggled from the in-book menu,
  // seeded from the global default (ON). Two independent flags: body = reader page
  // text, status = the reading-screen status bar.
  uint8_t paperbackLookBody = 1;
  uint8_t paperbackLookStatus = 1;
  // First-line paragraph indent (restored old-lector model): mode 0 = Book (respect
  // CSS indent), 1 = Custom % of the column width. Seeded from the global default.
  uint8_t firstLineIndentMode = 0;
  uint8_t firstLineIndentPercent = 0;
  // SD card font family name (empty = built-in fontFamily). Fixed width keeps the struct POD.
  char sdFontFamilyName[32] = "";

  // Snapshot the current global reader settings. Zero-pads sdFontFamilyName so the
  // trailing bytes are canonical and whole-blob memcmp change-detection is exact.
  static ReaderPrefs fromGlobal();
};

// ── Serialization: [uint8 version][POD blob] ──────────────────────────────────
// Stream overloads are header-inline and Arduino-free so the host tests exercise
// them directly. The HalFile (device) overloads share the identical layout and are
// defined in ReaderPrefs.cpp with checked byte counts.
inline void writeReaderPrefs(std::ostream& out, const ReaderPrefs& p) {
  const uint8_t ver = ReaderPrefs::VERSION;
  out.write(reinterpret_cast<const char*>(&ver), 1);
  out.write(reinterpret_cast<const char*>(&p), sizeof(ReaderPrefs));
}

inline bool readReaderPrefs(std::istream& in, ReaderPrefs& p) {
  uint8_t ver = 0;
  if (!in.read(reinterpret_cast<char*>(&ver), 1)) return false;
  if (ver != ReaderPrefs::VERSION) return false;
  ReaderPrefs tmp;
  if (!in.read(reinterpret_cast<char*>(&tmp), sizeof(ReaderPrefs))) return false;
  p = tmp;
  return true;
}

// Device (SD) overloads — defined in ReaderPrefs.cpp. HalFile is only forward-declared
// here so this header never pulls HalStorage/Arduino into the host test build.
class HalFile;
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p);
bool readReaderPrefs(HalFile& in, ReaderPrefs& p);
