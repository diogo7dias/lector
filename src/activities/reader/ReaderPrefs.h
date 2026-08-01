#pragma once
#include <cstdint>
#include <cstring>
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
// Reader font size became a real point size when upstream #2720 replaced the
// SMALL/MEDIUM/LARGE/EXTRA_LARGE slot. Sidecars and presets written before that
// hold the old slot in 0..3; no font renders at those sizes, so the range is
// unambiguous and folds to the point sizes those slots used to mean.
inline uint8_t foldLegacyReaderFontSize(const uint8_t stored) {
  return stored <= 3 ? static_cast<uint8_t>(12 + stored * 2) : stored;
}

struct ReaderPrefs {
  // Bump whenever the field set changes: readReaderPrefs rejects a mismatched
  // version, so an old sidecar is ignored and the book falls back to global.
  // v5 is the one exception — same layout, only fontPointSize's meaning changed,
  // so it is read and folded instead of dropped.
  static constexpr uint8_t VERSION = 6;  // v6: fontPointSize replaces the fontSize slot (upstream #2720)

  // Font (Family/Size tabs)
  uint8_t fontFamily = 0;      // CrossPointSettings::VOLLKORN
  uint8_t fontPointSize = 14;  // CrossPointSettings::DEFAULT_FONT_POINT_SIZE
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
  uint8_t textAntiAliasing = 0;  // see CrossPointSettings: the grey fade per page is not worth it
  // Fed into the render spec (edited from the Reader settings category, snapshotted here).
  uint8_t imageRendering = 0;  // CrossPointSettings::IMAGES_DISPLAY
  // Paragraph numbering — per-book, seeded from the global default and then overridable
  // from the in-book menu. 0 = off, 1 = per chapter, 2 = whole book.
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

// ── Mid-edit override decision ────────────────────────────────────────────────
// While the in-book Reader Settings screen is open, the edited values live on the
// global reader fields (see CrossPointSettings::beginReaderEditOverlay). Every row
// change must land on the card straight away, so switching the reader off inside
// that screen keeps the change instead of losing it.
//
// This is the pure rule behind that write, split out from the storage call so the
// host tests can exercise it: it takes the live overlaid values, the book's current
// prefs, and whether the book already has a sidecar, and says what to do with it.
enum class ReaderOverrideAction : uint8_t {
  Keep,    // the file on the card already says this — leave it alone
  Write,   // persist these prefs as the book's override
  Remove,  // the book follows global again; drop any sidecar written mid-edit
};

struct ReaderOverrideDecision {
  ReaderOverrideAction action;
  ReaderPrefs prefs;  // only meaningful for Write
};

inline ReaderOverrideDecision decideReaderOverride(const ReaderPrefs& live, const ReaderPrefs& book,
                                                   const bool bookIsCustom) {
  ReaderOverrideDecision decision{ReaderOverrideAction::Keep, live};
  // paragraphNumbering and the two Paperback flags are per-book in-menu toggles, not
  // rows of the Reader Settings screen. The overlay does not carry them, so `live`
  // holds the GLOBAL values for those three; the book's own must survive the edit.
  decision.prefs.paragraphNumbering = book.paragraphNumbering;
  decision.prefs.paperbackLookBody = book.paperbackLookBody;
  decision.prefs.paperbackLookStatus = book.paperbackLookStatus;

  if (std::memcmp(&decision.prefs, &book, sizeof(ReaderPrefs)) != 0) {
    decision.action = ReaderOverrideAction::Write;
  } else if (!bookIsCustom) {
    // Edited a row and then put it back: the book was following global, so it must go
    // on following global rather than freeze as custom on a sidecar written moments ago.
    decision.action = ReaderOverrideAction::Remove;
  }
  return decision;
}

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
  // Keep this accept-and-fold rule identical to the HalFile overload in
  // ReaderPrefs.cpp — they read the same on-card format.
  if (ver != ReaderPrefs::VERSION && ver != 5) return false;
  ReaderPrefs tmp;
  if (!in.read(reinterpret_cast<char*>(&tmp), sizeof(ReaderPrefs))) return false;
  if (ver == 5) tmp.fontPointSize = foldLegacyReaderFontSize(tmp.fontPointSize);
  p = tmp;
  return true;
}

// Device (SD) overloads — defined in ReaderPrefs.cpp. HalFile is only forward-declared
// here so this header never pulls HalStorage/Arduino into the host test build.
class HalFile;
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p);
bool readReaderPrefs(HalFile& in, ReaderPrefs& p);
