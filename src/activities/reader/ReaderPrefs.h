#pragma once
#include <Epub/ReaderRenderSpec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <string>

#include "ReaderLookFields.h"

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
inline constexpr uint8_t PARAGRAPH_NUMBERING_COUNT = 2;   // CrossPointSettings::PARAGRAPH_NUMBERING_COUNT
inline constexpr uint8_t PARAGRAPH_NUMBER_SIZE = 1;       // CrossPointSettings::PARA_NUM_SIZE_DOUBLE
// Ranges the render spec clamps to, shared with the settings rows that edit them.
inline constexpr uint8_t MIN_LINE_SPACING_PERCENT = 35;
inline constexpr uint8_t MAX_LINE_SPACING_PERCENT = 150;
inline constexpr uint8_t MIN_WORD_SPACING = 75;
inline constexpr uint8_t MAX_WORD_SPACING = 150;
}  // namespace reader_defaults

struct ReaderPrefs {
  // Bump whenever the field set changes: readReaderPrefs rejects a mismatched
  // version, so an old sidecar is ignored and the book falls back to global.
  // v5 through v9 are the exceptions: they are read and upgraded instead of dropped.
  // Dropping a sidecar silently discards every per-book setting the user ever chose,
  // which is far worse than carrying an old one forward. Each of those older layouts is
  // a strict prefix of this struct, so a record is read at its own length and every
  // field appended since keeps its constructed default — see readerPrefsRecordSize().
  static constexpr uint8_t VERSION = 14;  // v14: baseline word spacing

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
  uint8_t fontFamily = 0;      // CrossPointSettings::CHAREINK
  uint8_t fontPointSize = 14;  // CrossPointSettings::DEFAULT_FONT_POINT_SIZE
  // Layout tab
  uint8_t lineSpacingPercent = 100;  // % of natural line height (restored granular)
  uint8_t paragraphAlignment = 0;    // CrossPointSettings::JUSTIFIED
  uint8_t extraParagraphSpacing = 1;
  uint8_t paragraphSpacing = 0;  // % of line height (block gap; restored granular)
  uint8_t screenMargin = 20;     // horizontal (left/right), shared by both sides
  uint8_t screenMarginTop = 20;
  uint8_t screenMarginBottom = 20;
  // margin_link::Mode as a number: 0 = Separate, 1 = TopBottom, 2 = All Sides.
  uint8_t marginLinkMode = 2;
  uint8_t dynamicMargins = 0;  // 0 = off, 1 = auto (min 10px), 2 = auto (min 20px)
  // Style tab
  uint8_t focusReadingEnabled = 0;
  uint8_t guideDotsEnabled = 0;  // middle dot between words (restored)
  uint8_t guideDotsHidden = 0;   // keep the widened guide-dot gap, draw no dot in it
  // Retired setting byte: preserve every later offset in v5-v14 binary sidecars.
  uint8_t reserved14 = 0;
  uint8_t embeddedTextStyle = 1;
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
  // time this book gets a sidecar. The defaults below are the layout the firmware
  // shipped through 0.28 and they stay frozen there: a record written before v11 has no
  // stored layout, and a book read under that layout must go on looking the way it did
  // rather than jump to whatever the current firmware defaults to. New books are seeded
  // from CrossPointSettings and never reach these values.
  //
  // APPENDED LAST as one block. See the note above paragraphNumberSize.
  uint8_t sbBatteryPos = 4;       // CrossPointSettings::SB_ANCHOR_BL
  uint8_t sbClockPos = 0;         // SB_ANCHOR_OFF
  uint8_t sbTitlePos = 5;         // SB_ANCHOR_BC
  uint8_t sbTitleSource = 0;      // SB_TITLE_BOOK
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

  // The layout half of the old single "Embedded Style" switch: the book's own margins,
  // indents and block spacing, separate from its fonts and emphasis.
  //
  // APPENDED LAST. See the note above paragraphNumberSize. It belongs beside
  // embeddedTextStyle, but putting it there would shift every field below it and make
  // this firmware misread every v11 sidecar on the card.
  uint8_t embeddedLayoutStyle = 1;

  // Pages left in the current paragraph (">P.N") anchor. Belongs in the status bar
  // block above, but goes here for the same reason embeddedLayoutStyle did: putting
  // it there would shift every field below it and make this firmware misread every
  // v12 sidecar on the card.
  //
  // APPENDED LAST. See the note above paragraphNumberSize.
  uint8_t sbParaPagesPos = 0;  // SB_ANCHOR_OFF

  // Appended so v5-v13 sidecars retain every existing field and default to unchanged spacing.
  uint8_t wordSpacing = 100;

  // Copy the status bar block from `source`.
  //
  // A sidecar written before v11 stops before that block, so every field would
  // otherwise fall back to the constructed default: the layout this firmware ships
  // with, not the one the user configured. Reading a book with an older sidecar would
  // then silently rearrange its status bar. Migration seeds the block from the live
  // global settings instead, which is what the user already sees everywhere else.
  void adoptStatusBarFrom(const ReaderPrefs& source) {
#define CP_ADOPT_SB(prefsName, settingsName, blockName) prefsName = source.prefsName;
    READER_STATUS_BAR_FIELDS(CP_ADOPT_SB)
#undef CP_ADOPT_SB
  }

  // Snapshot the current global reader settings. Zero-pads sdFontFamilyName so the
  // trailing bytes are canonical and whole-blob memcmp change-detection is exact.
  static ReaderPrefs fromGlobal();
};

// ── The render spec a look produces ───────────────────────────────────────────
// Line-height multiplier from a line-spacing percentage (100 = natural), clamped to the
// range the settings row offers.
inline float readerLineCompression(const uint8_t lineSpacingPercent) {
  return static_cast<float>(std::clamp(lineSpacingPercent, reader_defaults::MIN_LINE_SPACING_PERCENT,
                                       reader_defaults::MAX_LINE_SPACING_PERCENT)) /
         100.0f;
}

// The one builder for every ReaderRenderSpec: the reader and the Text Settings preview
// both go through it. The font id is resolved by the caller because an SD family needs
// the live font registry; everything else comes from the look.
inline ReaderRenderSpec makeRenderSpec(const ReaderPrefs& p, const int fontId, const uint16_t viewportWidth,
                                       const uint16_t viewportHeight) {
  ReaderRenderSpec spec;
  spec.fontId = fontId;
  spec.lineCompression = readerLineCompression(p.lineSpacingPercent);
  spec.extraParagraphSpacing = p.extraParagraphSpacing != 0;
  spec.paragraphSpacing = p.paragraphSpacing;
  spec.wordSpacing = std::clamp(p.wordSpacing, reader_defaults::MIN_WORD_SPACING, reader_defaults::MAX_WORD_SPACING);
  spec.paragraphAlignment = p.paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.embeddedTextStyle = p.embeddedTextStyle != 0;
  spec.embeddedLayoutStyle = p.embeddedLayoutStyle != 0;
  // Hard-set to CrossPointSettings::IMAGES_DISPLAY, not read: see the note on
  // IMAGE_RENDERING. This is the choke point that makes a stored placeholder or suppress
  // value in a per-book override or a reader preset irrelevant without touching the
  // section file format.
  spec.imageRendering = 0;
  spec.focusReadingEnabled = p.focusReadingEnabled != 0;
  spec.guideDotsMode = resolveGuideDotsMode(p.guideDotsEnabled, p.guideDotsHidden);
  spec.firstLineIndentMode = p.firstLineIndentMode;
  spec.firstLineIndentPercent = p.firstLineIndentPercent;
  return spec;
}

// On-card size of each older blob: this struct truncated at the field the next version
// appended. They live out here because offsetof needs the completed type.
inline constexpr size_t READER_PREFS_V8_SIZE = offsetof(ReaderPrefs, paragraphNumberSize);
inline constexpr size_t READER_PREFS_V9_SIZE = offsetof(ReaderPrefs, statusBarEnabled);
inline constexpr size_t READER_PREFS_V10_SIZE = offsetof(ReaderPrefs, sbBatteryPos);
inline constexpr size_t READER_PREFS_V11_SIZE = offsetof(ReaderPrefs, embeddedLayoutStyle);
inline constexpr size_t READER_PREFS_V12_SIZE = offsetof(ReaderPrefs, sbParaPagesPos);
inline constexpr size_t READER_PREFS_V13_SIZE = offsetof(ReaderPrefs, wordSpacing);

// A record older than v12 carries one "Embedded Style" choice, in what is now the text
// switch. Someone who turned it off wanted the book's own styling gone, so the layout
// switch follows it rather than quietly handing back the book's margins and indents.
inline constexpr uint8_t FIRST_VERSION_WITH_SPLIT_EMBEDDED_STYLE = 12;
// A record older than v10 has no status bar on/off byte, and one older than v11 has no
// status bar layout block. Only those are seeded from the global settings on migration:
// doing it for any upgraded record would replace a per-book bar the reader configured.
inline constexpr uint8_t FIRST_VERSION_WITH_STATUS_BAR_SWITCH = 10;
inline constexpr uint8_t FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR = 11;
// `layoutStyleAlreadyRead` is set only for the interim v11 layout, whose record really
// does carry its own layout switch: seeding it from the text switch there would throw
// away a choice the reader made.
inline void migrateReaderPrefsFields(const uint8_t version, ReaderPrefs& p, const bool layoutStyleAlreadyRead = false) {
  p.reserved14 = 0;  // Canonical padding for whole-blob change detection.
  if (version == 5) p.fontPointSize = foldLegacyReaderFontSize(p.fontPointSize);
  if (!layoutStyleAlreadyRead && version < FIRST_VERSION_WITH_SPLIT_EMBEDDED_STYLE) {
    p.embeddedLayoutStyle = p.embeddedTextStyle;
  }
  if (version < ReaderPrefs::FIRST_VERSION_WITH_CURRENT_DEFAULTS) p.adoptCurrentReadingDefaults();
}

// Bytes to read for a record written by `version`, or 0 when that version cannot be
// read at all. One rule, shared by the stream and HalFile overloads, so the host tests
// pin the exact behaviour the device gets. Reading sizeof() for an older record would
// run off the end of it and drop every per-book setting the user ever chose.
// Between the Embedded Style split and the version bump that should have come with it,
// main wrote records STAMPED 11 that carry embeddedLayoutStyle in the middle of the
// struct instead of at its end. Such a record is one byte longer than a real v11 one,
// and length is the only thing that tells the two apart: read at the stable v11 length
// it succeeds, having shifted every field after embeddedTextStyle by one byte.
//
// Move the stray byte back to the end so the record reads as what it meant.
inline constexpr size_t READER_PREFS_INTERIM_V11_SIZE = READER_PREFS_V11_SIZE + 1;
inline void unshiftInterimV11(const uint8_t* record, ReaderPrefs& out) {
  // The byte sat where textAntiAliasing now begins, directly after embeddedTextStyle.
  constexpr size_t split = offsetof(ReaderPrefs, textAntiAliasing);
  auto* raw = reinterpret_cast<uint8_t*>(&out);
  std::memcpy(raw, record, split);
  std::memcpy(raw + split, record + split + 1, READER_PREFS_V11_SIZE - split);
  out.embeddedLayoutStyle = record[split];
}

inline constexpr size_t readerPrefsRecordSize(const uint8_t version) {
  if (version >= 5 && version <= 8) return READER_PREFS_V8_SIZE;
  if (version == 9) return READER_PREFS_V9_SIZE;
  if (version == 10) return READER_PREFS_V10_SIZE;
  if (version == 11) return READER_PREFS_V11_SIZE;
  if (version == 12) return READER_PREFS_V12_SIZE;
  if (version == 13) return READER_PREFS_V13_SIZE;
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
static_assert(READER_PREFS_V11_SIZE == READER_PREFS_V10_SIZE + 17,
              "the v11 status bar block must be 17 bytes, with no padding before it");
static_assert(READER_PREFS_V12_SIZE == READER_PREFS_V11_SIZE + 1,
              "embeddedLayoutStyle must sit between the v11 and v12 record ends, with no padding");
static_assert(READER_PREFS_V13_SIZE == READER_PREFS_V12_SIZE + 1, "v13 adds sbParaPagesPos");
static_assert(sizeof(ReaderPrefs) == READER_PREFS_V13_SIZE + 1,
              "wordSpacing must be the last byte: every new field goes last, or "
              "this firmware misreads every sidecar written by the version before it");

// ── The field lists cover the struct ──────────────────────────────────────────
// Every live byte of ReaderPrefs is either the fixed-width font name or a uint8_t named by
// exactly one of the three lists in ReaderLookFields.h. That is what makes a missed
// copier impossible: a field added to the struct and to no list fails this assert, and
// a field added to two lists fails it as well.
namespace reader_look {
#define CP_COUNT_FIELD(...) +1
inline constexpr size_t SCREEN_FIELD_COUNT = 0 READER_LOOK_SCREEN_FIELDS(CP_COUNT_FIELD);
inline constexpr size_t BOOK_FIELD_COUNT = 0 READER_LOOK_BOOK_FIELDS(CP_COUNT_FIELD);
inline constexpr size_t STATUS_BAR_FIELD_COUNT = 0 READER_STATUS_BAR_FIELDS(CP_COUNT_FIELD);
#undef CP_COUNT_FIELD
}  // namespace reader_look

static_assert(reader_look::SCREEN_FIELD_COUNT + reader_look::BOOK_FIELD_COUNT + reader_look::STATUS_BAR_FIELD_COUNT +
                      sizeof(ReaderPrefs::sdFontFamilyName) + sizeof(ReaderPrefs::reserved14) ==
                  sizeof(ReaderPrefs),
              "every ReaderPrefs field must appear in exactly one list in ReaderLookFields.h, or "
              "some copier will silently drop it");

// Restore the fields the Reader Settings screen never edits: the in-book toggles and
// the status bar switch. The book's own must survive the edit; this is the one place
// that says which fields those are.
inline void restoreBookOnlyFields(ReaderPrefs& target, const ReaderPrefs& book) {
#define CP_RESTORE_BOOK_FIELD(name) target.name = book.name;
  READER_LOOK_BOOK_FIELDS(CP_RESTORE_BOOK_FIELD)
#undef CP_RESTORE_BOOK_FIELD
  target.statusBarEnabled = book.statusBarEnabled;
}

// The whole status bar configuration as one value. Everything that draws or measures
// the bar is handed the block it should use: a book's own, or the global one.
struct StatusBarBlock {
  uint8_t enabled = 1;
  uint8_t batteryPos = 0;
  uint8_t clockPos = 0;
  uint8_t titlePos = 0;
  uint8_t titleSource = 0;
  uint8_t titleTruncate = 0;
  uint8_t pagePos = 0;
  uint8_t pageFormat = 0;
  uint8_t bookPctPos = 0;
  uint8_t chapterPctPos = 0;
  uint8_t chapterNumPos = 0;
  uint8_t sessionPagesPos = 0;
  uint8_t paraPagesPos = 0;
  uint8_t bookBar = 0;
  uint8_t chapterBar = 0;
  uint8_t barThickness = 0;
  uint8_t floatingBar = 0;
  uint8_t barOutline = 0;
  uint8_t offBar = 0;

  bool textOn() const { return enabled != 0; }
  // ── Progress bars while the status bar is hidden ───────────────────────────
  // The Book Bar / Chapter Bar edges are part of the status bar, so hiding the bar
  // used to hide them as well. offBar keeps them alive on their own: the edges,
  // the percentages and the draw path stay exactly as they are with the bar showing,
  // only the visibility gate and the thickness come from a different field.
  //
  // Everything that draws or reserves space for a progress bar must ask
  // progressBarsVisible() + activeBarThickness(), never textOn() + barThickness, or
  // the reserved band and the drawn bar disagree and the bar paints over the text.
  bool progressBarsVisible() const { return textOn() || offBar != OFF_BAR_OFF; }
  uint8_t activeBarThickness() const {
    if (textOn() || offBar == OFF_BAR_OFF) return barThickness;
    return static_cast<uint8_t>(offBar - 1);  // Slim/Medium/Fat -> 0/1/2
  }
  // Gap between a floating progress bar and the screen edge, in pixels. Every site that
  // draws OR reserves space for a bar must add it, or the two disagree.
  static constexpr int FLOATING_BAR_MARGIN_PX = 12;
  int floatingBarMarginPx() const { return floatingBar ? FLOATING_BAR_MARGIN_PX : 0; }
  // CrossPointSettings::SB_OFFBAR_OFF, asserted equal there.
  static constexpr uint8_t OFF_BAR_OFF = 0;
};

// The block and the field list must agree: the three copiers below expand the list, so a
// field added to StatusBarBlock alone would never be copied anywhere.
static_assert(sizeof(StatusBarBlock) == reader_look::STATUS_BAR_FIELD_COUNT,
              "every StatusBarBlock field must appear in READER_STATUS_BAR_FIELDS");

// A book's status bar, and writing one back into its look.
inline StatusBarBlock statusBarOf(const ReaderPrefs& p) {
  StatusBarBlock b;
#define CP_SB_FROM_PREFS(prefsName, settingsName, blockName) b.blockName = p.prefsName;
  READER_STATUS_BAR_FIELDS(CP_SB_FROM_PREFS)
#undef CP_SB_FROM_PREFS
  return b;
}
inline void setStatusBarOf(ReaderPrefs& p, const StatusBarBlock& b) {
#define CP_SB_TO_PREFS(prefsName, settingsName, blockName) p.prefsName = b.blockName;
  READER_STATUS_BAR_FIELDS(CP_SB_TO_PREFS)
#undef CP_SB_TO_PREFS
}

// ── A book's look on open ─────────────────────────────────────────────────────
// Whether the book's stylesheet has to be parsed. Decided from the book's own look:
// a book with either embedded switch on needs its CSS even when both global switches
// are off, and the loader never builds the CSS cache for a book that skipped it.
inline bool wantsBookCss(const ReaderPrefs& p) { return p.embeddedTextStyle || p.embeddedLayoutStyle; }

// Brings a sidecar record read at `fromVersion` in line with this firmware.
// - A record written before whole-book numbering was removed can still say 2; the
//   Settings row and the menu cycle can no longer produce it.
// - A record older than v11 stops before the status bar block, so those fields came
//   back as this firmware's shipped defaults rather than the layout the user
//   configured. They are seeded from the global look, the bar every other book shows.
inline void settleSidecarPrefs(ReaderPrefs& p, const uint8_t fromVersion, const ReaderPrefs& global) {
  if (p.paragraphNumbering >= reader_defaults::PARAGRAPH_NUMBERING_COUNT) {
    p.paragraphNumbering = reader_defaults::PARAGRAPH_NUMBERING;
  }
  if (fromVersion < FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR) p.adoptStatusBarFrom(global);
}

// ── Mid-edit override decision ────────────────────────────────────────────────
// While the in-book Reader Settings screen is open, every row change must land on the
// card straight away, so switching the reader off inside that screen keeps the change
// instead of losing it.
//
// This is the pure rule behind that write, split out from the storage call so the
// host tests can exercise it: it takes the edited values, the book's current prefs,
// and whether the book already has a sidecar, and says what to do with it.
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
  // The screen never edits the in-book toggles or the status bar switch; the book's own
  // must survive the edit whatever `live` carries for them.
  restoreBookOnlyFields(decision.prefs, book);

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
inline bool readReaderPrefs(std::istream& in, ReaderPrefs& p, bool* migrated = nullptr,
                            uint8_t* fromVersion = nullptr) {
  if (migrated) *migrated = false;
  uint8_t ver = 0;
  if (!in.read(reinterpret_cast<char*>(&ver), 1)) return false;
  if (fromVersion) *fromVersion = ver;
  // Keep this accept-and-fold rule identical to the HalFile overload in
  // ReaderPrefs.cpp — they read the same on-card format.
  const size_t want = readerPrefsRecordSize(ver);
  if (want == 0) return false;
  // An older record is shorter, so read only what it actually holds and leave every
  // field appended since at its constructed default.
  ReaderPrefs tmp;
  bool interimV11 = false;
  if (ver == 11) {
    // Read one byte past the stable v11 record: getting it means this is the interim
    // layout, which is exactly one byte longer.
    uint8_t record[READER_PREFS_INTERIM_V11_SIZE] = {};
    in.read(reinterpret_cast<char*>(record), READER_PREFS_INTERIM_V11_SIZE);
    const auto got = static_cast<size_t>(in.gcount());
    in.clear();  // reading past a stable v11 record sets eofbit, which is not a failure
    if (got < READER_PREFS_V11_SIZE) return false;
    interimV11 = got == READER_PREFS_INTERIM_V11_SIZE;
    if (interimV11) {
      unshiftInterimV11(record, tmp);
    } else {
      std::memcpy(&tmp, record, READER_PREFS_V11_SIZE);
    }
  } else if (!in.read(reinterpret_cast<char*>(&tmp), want)) {
    return false;
  }
  migrateReaderPrefsFields(ver, tmp, interimV11);
  if (ver < ReaderPrefs::VERSION && migrated) *migrated = true;
  p = tmp;
  return true;
}

// Device (SD) overloads — defined in ReaderPrefs.cpp. HalFile is only forward-declared
// here so this header never pulls HalStorage/Arduino into the host test build.
class HalFile;
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p);
bool readReaderPrefs(HalFile& in, ReaderPrefs& p, bool* migrated = nullptr, uint8_t* fromVersion = nullptr);

// Where a book keeps its own look, beside the rest of its cache.
inline std::string readerSidecarPath(const std::string& cachePath) { return cachePath + "/reader_override.bin"; }

// A book's look as it opens: its sidecar when one is on the card and readable, else
// `global`. Defined in ReaderPrefs.cpp (reads the SD card).
struct BookReaderPrefs {
  ReaderPrefs prefs;
  bool custom = false;    // the book has its own sidecar
  bool migrated = false;  // that sidecar was upgraded on the way in
  uint8_t fromVersion = ReaderPrefs::VERSION;
};
BookReaderPrefs loadBookReaderPrefs(const std::string& cachePath, const ReaderPrefs& global);
