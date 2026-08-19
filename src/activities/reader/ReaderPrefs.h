#pragma once
#include <cstddef>
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

// The reading defaults introduced in 0.8.1, kept here rather than in CrossPointSettings
// because both the global settings and the per-book sidecar upgrade need them, and this
// header is the one of the two that the host tests can compile on its own.
namespace reader_defaults {
// The separate "Paragraph Spacing %" control was removed in 0.8.2: the half-line gap that
// Extra Paragraph Spacing already adds, plus the always-on first-line indent, is the whole
// paragraph break. The field and its cache key stay so old sidecars still deserialize.
inline constexpr uint8_t PARAGRAPH_SPACING_PERCENT = 0;
inline constexpr uint8_t EXTRA_PARAGRAPH_SPACING = 1;     // half a line of air between paragraphs, on
inline constexpr uint8_t FIRST_LINE_INDENT_PERCENT = 20;  // % of the column width
inline constexpr uint8_t FIRST_LINE_INDENT_MODE = 1;      // CrossPointSettings::FIRST_LINE_INDENT_PERCENT
inline constexpr uint8_t PARAGRAPH_NUMBERING = 1;         // CrossPointSettings::PARA_NUM_CHAPTER
inline constexpr uint8_t PARAGRAPH_NUMBER_SIZE = 1;       // CrossPointSettings::PARA_NUM_SIZE_DOUBLE
}  // namespace reader_defaults

struct ReaderPrefs {
  // Bump whenever the field set changes: readReaderPrefs rejects a mismatched
  // version, so an old sidecar is ignored and the book falls back to global.
  // v5 through v9 are the exceptions: they are read and upgraded instead of dropped.
  // Dropping a sidecar silently discards every per-book setting the user ever chose,
  // which is far worse than carrying an old one forward. Each of those older layouts is
  // a strict prefix of this struct, so a record is read at its own length and every
  // field appended since keeps its constructed default — see readerPrefsRecordSize().
  static constexpr uint8_t VERSION = 11;  // v11: per-book status bar layout

  // Bring a sidecar written before the current version onto the current reading
  // defaults. Only these values are re-seeded, and only for books that predate them.
  // Everything else the user chose is left as it was.
  //
  // Every one of these fields existed by v9, so only a v5-v8 record can predate them.
  // Gating on that rather than on "older than VERSION" is what keeps a later version
  // bump from silently re-seeding a book that already made these choices.
  static constexpr uint8_t FIRST_VERSION_WITH_CURRENT_DEFAULTS = 9;
  void adoptCurrentReadingDefaults() {
    paragraphSpacing = reader_defaults::PARAGRAPH_SPACING_PERCENT;
    extraParagraphSpacing = reader_defaults::EXTRA_PARAGRAPH_SPACING;
    firstLineIndentMode = reader_defaults::FIRST_LINE_INDENT_MODE;
    firstLineIndentPercent = reader_defaults::FIRST_LINE_INDENT_PERCENT;
    paragraphNumbering = reader_defaults::PARAGRAPH_NUMBERING;
    paragraphNumberSize = reader_defaults::PARAGRAPH_NUMBER_SIZE;
  }

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
  uint8_t guideDotsHidden = 0;   // keep the widened guide-dot gap, draw no dot in it
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
  // Size of the paragraph numbers: 0 = Small (Spleen's native 12px cell, 8px digits),
  // 1 = Double (that cell at exactly 2x, 16px digits). Per-book in-menu choice like
  // paragraphNumbering, seeded from the global default.
  //
  // APPENDED ON PURPOSE. Every field above it keeps its offset, so a v5-v8 sidecar
  // is exactly this struct up to here and can be read straight into the front of it
  // (see READER_PREFS_V8_SIZE below). Any future field must also go last, for the
  // same reason.
  uint8_t paragraphNumberSize = 1;  // CrossPointSettings::PARA_NUM_SIZE_DOUBLE
  // Status bar on/off for this book, seeded from the global SETTINGS.sbEnabled.
  // Turning it off frees the reserved top/bottom bands, which changes the viewport and
  // therefore repaginates this book's cache like any margin change.
  //
  // APPENDED LAST. See the note above paragraphNumberSize.
  uint8_t statusBarEnabled = 1;

  // The whole status bar layout, per book, seeded from the global settings the first
  // time this book gets a sidecar. Defaults below mirror CrossPointSettings so a
  // record written before v11 falls back to the same layout the firmware ships with,
  // rather than to a blank bar.
  //
  // APPENDED LAST as one block. See the note above paragraphNumberSize.
  uint8_t sbBatteryPos = 4;       // CrossPointSettings::SB_ANCHOR_BL
  uint8_t sbClockPos = 0;         // SB_ANCHOR_OFF
  uint8_t sbTitlePos = 5;         // SB_ANCHOR_BC
  uint8_t sbTitleSource = 0;      // SB_TITLE_CHAPTER
  uint8_t sbTitleTruncate = 0;    // greedy, no ellipsis
  uint8_t sbPagePos = 6;          // SB_ANCHOR_BR
  uint8_t sbPageFormat = 0;       // SB_PAGE_FRACTION
  uint8_t sbBookPctPos = 6;       // SB_ANCHOR_BR
  uint8_t sbChapterPctPos = 0;    // SB_ANCHOR_OFF
  uint8_t sbChapterNumPos = 0;    // SB_ANCHOR_OFF
  uint8_t sbSessionPagesPos = 0;  // SB_ANCHOR_OFF
  uint8_t sbBookBar = 0;          // SB_EDGE_OFF
  uint8_t sbChapterBar = 0;       // SB_EDGE_OFF
  uint8_t sbBarThickness = 1;     // SB_BAR_MEDIUM
  uint8_t sbFloatingBar = 0;
  uint8_t sbBarOutline = 0;
  uint8_t sbOffBar = 0;  // SB_OFFBAR_OFF

  // Copy the status bar block from `source`.
  //
  // A sidecar written before v11 stops before that block, so every field would
  // otherwise fall back to the constructed default: the layout this firmware ships
  // with, not the one the user configured. Reading a book with an older sidecar would
  // then silently rearrange its status bar. Migration seeds the block from the live
  // global settings instead, which is what the user already sees everywhere else.
  void adoptStatusBarFrom(const ReaderPrefs& source) {
    statusBarEnabled = source.statusBarEnabled;
    sbBatteryPos = source.sbBatteryPos;
    sbClockPos = source.sbClockPos;
    sbTitlePos = source.sbTitlePos;
    sbTitleSource = source.sbTitleSource;
    sbTitleTruncate = source.sbTitleTruncate;
    sbPagePos = source.sbPagePos;
    sbPageFormat = source.sbPageFormat;
    sbBookPctPos = source.sbBookPctPos;
    sbChapterPctPos = source.sbChapterPctPos;
    sbChapterNumPos = source.sbChapterNumPos;
    sbSessionPagesPos = source.sbSessionPagesPos;
    sbBookBar = source.sbBookBar;
    sbChapterBar = source.sbChapterBar;
    sbBarThickness = source.sbBarThickness;
    sbFloatingBar = source.sbFloatingBar;
    sbBarOutline = source.sbBarOutline;
    sbOffBar = source.sbOffBar;
  }

  // Snapshot the current global reader settings. Zero-pads sdFontFamilyName so the
  // trailing bytes are canonical and whole-blob memcmp change-detection is exact.
  static ReaderPrefs fromGlobal();
};

// On-card size of each older blob: this struct truncated at the field the next version
// appended. They live out here because offsetof needs the completed type.
inline constexpr size_t READER_PREFS_V8_SIZE = offsetof(ReaderPrefs, paragraphNumberSize);
inline constexpr size_t READER_PREFS_V9_SIZE = offsetof(ReaderPrefs, statusBarEnabled);
inline constexpr size_t READER_PREFS_V10_SIZE = offsetof(ReaderPrefs, sbBatteryPos);

// Bytes to read for a record written by `version`, or 0 when that version cannot be
// read at all. One rule, shared by the stream and HalFile overloads, so the host tests
// pin the exact behaviour the device gets. Reading sizeof() for an older record would
// run off the end of it and drop every per-book setting the user ever chose.
inline constexpr size_t readerPrefsRecordSize(const uint8_t version) {
  if (version >= 5 && version <= 8) return READER_PREFS_V8_SIZE;
  if (version == 9) return READER_PREFS_V9_SIZE;
  if (version == 10) return READER_PREFS_V10_SIZE;
  if (version == ReaderPrefs::VERSION) return sizeof(ReaderPrefs);
  return 0;
}

// Every member is a single byte or a char array, so the struct has alignment 1 and no
// padding anywhere. That is what makes each of those sizes equal the old sizeof
// exactly, and what makes the whole blob safe to memcmp and to read back on RISC-V.
static_assert(alignof(ReaderPrefs) == 1, "ReaderPrefs must stay byte-aligned POD");
static_assert(READER_PREFS_V9_SIZE == READER_PREFS_V8_SIZE + 1,
              "paragraphNumberSize must sit between the v8 and v9 record ends, with no padding");
static_assert(READER_PREFS_V10_SIZE == READER_PREFS_V9_SIZE + 1,
              "statusBarEnabled must sit between the v9 and v10 record ends, with no padding");
static_assert(sizeof(ReaderPrefs) == READER_PREFS_V10_SIZE + 17,
              "the v11 status bar block must be the last 17 bytes, with no padding before it");

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
  // The two paragraph-number fields, the two Paperback flags and the status bar switch
  // are per-book in-menu toggles, not rows of the Reader Settings screen. The overlay
  // does not carry them, so `live` holds the GLOBAL values for those five; the book's
  // own must survive the edit.
  decision.prefs.paragraphNumbering = book.paragraphNumbering;
  decision.prefs.paragraphNumberSize = book.paragraphNumberSize;
  decision.prefs.paperbackLookBody = book.paperbackLookBody;
  decision.prefs.paperbackLookStatus = book.paperbackLookStatus;
  decision.prefs.statusBarEnabled = book.statusBarEnabled;

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

// `migrated`, when given, reports whether the sidecar was upgraded on the way in, so
// the caller can rewrite it and re-anchor the reading position against the new layout.
inline bool readReaderPrefs(std::istream& in, ReaderPrefs& p, bool* migrated = nullptr) {
  if (migrated) *migrated = false;
  uint8_t ver = 0;
  if (!in.read(reinterpret_cast<char*>(&ver), 1)) return false;
  // Keep this accept-and-fold rule identical to the HalFile overload in
  // ReaderPrefs.cpp — they read the same on-card format.
  const size_t want = readerPrefsRecordSize(ver);
  if (want == 0) return false;
  // An older record is shorter, so read only what it actually holds and leave every
  // field appended since at its constructed default.
  ReaderPrefs tmp;
  if (!in.read(reinterpret_cast<char*>(&tmp), want)) return false;
  if (ver == 5) tmp.fontPointSize = foldLegacyReaderFontSize(tmp.fontPointSize);
  if (ver < ReaderPrefs::FIRST_VERSION_WITH_CURRENT_DEFAULTS) tmp.adoptCurrentReadingDefaults();
  if (ver < ReaderPrefs::VERSION && migrated) *migrated = true;
  p = tmp;
  return true;
}

// Device (SD) overloads — defined in ReaderPrefs.cpp. HalFile is only forward-declared
// here so this header never pulls HalStorage/Arduino into the host test build.
class HalFile;
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p);
bool readReaderPrefs(HalFile& in, ReaderPrefs& p, bool* migrated = nullptr);
