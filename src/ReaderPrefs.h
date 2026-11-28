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
#include "util/MarginLink.h"

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
inline constexpr uint8_t EXTRA_PARAGRAPH_SPACING = 1;    // half a line of air between paragraphs, on
inline constexpr uint8_t PARAGRAPH_NUMBERING = 1;        // CrossPointSettings::PARA_NUM_CHAPTER
inline constexpr uint8_t PARAGRAPH_NUMBERING_COUNT = 2;  // CrossPointSettings::PARAGRAPH_NUMBERING_COUNT
inline constexpr uint8_t PARAGRAPH_NUMBER_SIZE = 1;      // CrossPointSettings::PARA_NUM_SIZE_MEDIUM
// First-line indent counted in spaces of the reading font (CrossPoint's own unit).
inline constexpr uint8_t PARAGRAPH_INDENT_SPACES = 2;
inline constexpr uint8_t MAX_PARAGRAPH_INDENT_SPACES = 5;
// Ranges the render spec clamps to, shared with the settings rows that edit them.
inline constexpr uint8_t MIN_LINE_SPACING_PERCENT = 80;
inline constexpr uint8_t MAX_LINE_SPACING_PERCENT = 175;
inline constexpr uint8_t MIN_WORD_SPACING = READER_WORD_SPACING_MIN;
inline constexpr uint8_t MAX_WORD_SPACING = READER_WORD_SPACING_MAX;
inline constexpr uint8_t WORD_SPACING_STEP = 25;
inline constexpr uint8_t WORD_SPACING = 100;  // percent of the font's natural space advance
// Character Spacing is stored unsigned: whole pixels between letters plus this, so 2 is
// the font as designed and 0..4 covers -2..+2.
inline constexpr uint8_t CHARACTER_SPACING = 2;
inline constexpr uint8_t MAX_CHARACTER_SPACING = 4;
// Ink Weight is stored unsigned like Character Spacing: 0..4 covers -2..+2, with 2 = 0 (normal).
inline constexpr uint8_t INK_WEIGHT = 2;
inline constexpr uint8_t MAX_INK_WEIGHT = 4;
inline constexpr int8_t resolveInkWeight(const uint8_t stored) {
  const uint8_t clamped = stored > MAX_INK_WEIGHT ? MAX_INK_WEIGHT : stored;
  return static_cast<int8_t>(clamped) - static_cast<int8_t>(INK_WEIGHT);
}
// The side margin: 5 to 40 px in steps of 5. Top and bottom keep the wider 0 to 100 range.
inline constexpr uint8_t MIN_SCREEN_MARGIN = 5;
inline constexpr uint8_t MAX_SCREEN_MARGIN = 40;
inline constexpr uint8_t SCREEN_MARGIN_STEP = 5;
inline constexpr uint8_t SCREEN_MARGIN = 20;
// Image rendering values; CrossPointSettings::IMAGE_RENDERING repeats them (asserted there).
inline constexpr uint8_t IMAGES_DISPLAY_BW = 3;
inline constexpr uint8_t IMAGE_RENDERING_COUNT = 4;

// `value` clamped into [lo, hi] and moved to the nearest lo + n * step. Saved values from
// before a range or step changed land on the new grid through this.
inline constexpr uint8_t snapToStep(const int value, const int lo, const int hi, const int step) {
  const int clamped = value < lo ? lo : (value > hi ? hi : value);
  return static_cast<uint8_t>(lo + ((clamped - lo + step / 2) / step) * step);
}

// A first-line indent saved as "Book" or a percent of the column width, in whole spaces.
// A space is a few pixels, so the percent maps by tenths: the old default of 20 or 23
// percent lands on the new default of 2, 100 percent on 5, and anything above zero keeps
// at least one space. Book mode followed the publisher's CSS, which no longer exists as a
// choice, so it takes the default.
inline constexpr uint8_t indentSpacesFromLegacy(const uint8_t mode, const uint8_t percent) {
  if (mode == 0) return PARAGRAPH_INDENT_SPACES;
  if (percent == 0) return 0;
  const int spaces = (percent + 5) / 10;
  return static_cast<uint8_t>(
      spaces < 1 ? 1 : (spaces > MAX_PARAGRAPH_INDENT_SPACES ? MAX_PARAGRAPH_INDENT_SPACES : spaces));
}

// Half pixels between letters, stored with offset 5 (0..10), in whole pixels with offset 2
// (0..4): rounded to the nearest pixel, ties away from zero, then clamped.
inline constexpr uint8_t characterSpacingFromLegacy(const uint8_t storedHalfPixels) {
  const int half = static_cast<int>(storedHalfPixels) - 5;
  int px = (half >= 0 ? half + 1 : half - 1) / 2;
  px = px < -2 ? -2 : (px > 2 ? 2 : px);
  return static_cast<uint8_t>(px + CHARACTER_SPACING);
}
}  // namespace reader_defaults

struct ReaderPrefs {
  // Bump whenever the field set changes: readReaderPrefs rejects a mismatched
  // version, so an old sidecar is ignored and the book falls back to global.
  // v5 through v26 are the exceptions: they are read and upgraded instead of dropped.
  // Dropping a sidecar silently discards every per-book setting the user ever chose,
  // which is far worse than carrying an old one forward. Each of those older layouts is
  // a strict prefix of this struct, so a record is read at its own length and every
  // field appended since keeps its constructed default — see readerPrefsRecordSize().
  //
  // v27 keeps v26's byte layout and changes what some bytes mean: the retired switches
  // (paragraph gap, kerning, ligatures, link underline, book margins, heading page break,
  // word expansion, fill pages, the layout half of the style switch, indent mode) stay as
  // zeroed padding, the indent byte counts spaces, character spacing counts whole pixels.
  // v28: 3-way embeddedStyle (Off / Text Only / Full), same bytes as v27.
  // v29 appends inkWeight (0..4 stored, -2..+2 shown, default 2 = 0).
  static constexpr uint8_t VERSION = 30;  // v30: keepHeadingsWithText

  // Bring a sidecar written before the current version onto the current reading
  // defaults. Only these values are re-seeded, and only for books that predate them.
  // Everything else the user chose is left as it was.
  //
  // Every one of these fields existed by v9, so only a v5-v8 record can predate them.
  // Gating on that rather than on "older than VERSION" is what keeps a later version
  // bump from silently re-seeding a book that already made these choices.
  static constexpr uint8_t FIRST_VERSION_WITH_CURRENT_DEFAULTS = 9;
  void adoptCurrentReadingDefaults() {
    extraParagraphSpacing = reader_defaults::EXTRA_PARAGRAPH_SPACING;
    paragraphIndentSpaces = reader_defaults::PARAGRAPH_INDENT_SPACES;
    paragraphNumbering = reader_defaults::PARAGRAPH_NUMBERING;
    paragraphNumberSize = reader_defaults::PARAGRAPH_NUMBER_SIZE;
  }

  // Font (Family/Size tabs)
  uint8_t fontFamily = 0;      // CrossPointSettings::CHAREINK
  uint8_t fontPointSize = 14;  // CrossPointSettings::DEFAULT_FONT_POINT_SIZE
  // Layout tab
  uint8_t lineSpacingPercent = 100;  // % of natural line height
  uint8_t paragraphAlignment = 0;    // CrossPointSettings::JUSTIFIED
  uint8_t extraParagraphSpacing = 1;
  uint8_t retiredParagraphSpacing = 0;                    // was the paragraph gap in % of line height
  uint8_t screenMargin = reader_defaults::SCREEN_MARGIN;  // horizontal (left/right), shared by both sides
  uint8_t screenMarginTop = reader_defaults::SCREEN_MARGIN;
  uint8_t screenMarginBottom = reader_defaults::SCREEN_MARGIN;
  // margin_link::Mode as a number: 0 = Separate, 1 = TopBottom, 2 = All Sides.
  uint8_t marginLinkMode = 2;
  uint8_t dynamicMargins = 0;  // 0 = off, 1 = auto (min 10px), 2 = auto (min 20px)
  // Style tab
  uint8_t focusReadingEnabled = 0;
  uint8_t guideDotsEnabled = 0;  // middle dot between words (restored)
  uint8_t guideDotsHidden = 0;   // keep the widened guide-dot gap, draw no dot in it
  // Retired setting byte: preserve every later offset in v5-v14 binary sidecars.
  uint8_t reserved14 = 0;
  uint8_t embeddedStyle = EMBEDDED_STYLE_FULL;  // EmbeddedStyleMode: Off / Text Only / Full
  // Retired: Text Anti-Aliasing (always off since 2026-11-22: its grey pass was 467 ms of a 1215 ms turn).
  uint8_t retiredTextAntiAliasing = 0;
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
  // First-line paragraph indent: the old mode byte is retired padding, the old percent byte
  // now counts spaces (0 to 5). Seeded from the global default.
  uint8_t retiredFirstLineIndentMode = 0;
  uint8_t paragraphIndentSpaces = reader_defaults::PARAGRAPH_INDENT_SPACES;
  // SD card font family name (empty = built-in fontFamily). Fixed width keeps the struct POD.
  char sdFontFamilyName[32] = "";
  // Size of the paragraph numbers as a share of the reading font: 0 = 35%, 1 = 65%,
  // 2 = 100% (once Small and Double, hence the order). Per-book in-menu choice like
  // paragraphNumbering, seeded from the global default.
  //
  // APPENDED ON PURPOSE. Every field above it keeps its offset, so a v5-v8 sidecar
  // is exactly this struct up to here and can be read straight into the front of it
  // (see READER_PREFS_V8_SIZE below). Any future field must also go last, for the
  // same reason.
  uint8_t paragraphNumberSize = 1;  // CrossPointSettings::PARA_NUM_SIZE_MEDIUM
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
  // Retired options (Floating Bar, Bar Outline), no longer read. The bytes stay so every
  // later offset in the binary sidecar holds.
  uint8_t sbFloatingBar = 0;
  uint8_t sbBarOutline = 0;
  uint8_t sbOffBar = 0;  // SB_OFFBAR_OFF

  // Retired: the layout half of the Embedded Style switch, folded into embeddedStyle by v27.
  uint8_t retiredEmbeddedLayoutStyle = 0;

  // Pages left in the current paragraph (">P.N") anchor. Belongs in the status bar
  // block above, but goes here for the same reason embeddedLayoutStyle did: putting
  // it there would shift every field below it and make this firmware misread every
  // v12 sidecar on the card.
  //
  // APPENDED LAST. See the note above paragraphNumberSize.
  uint8_t sbParaPagesPos = 0;  // SB_ANCHOR_OFF

  // Appended so v5-v13 sidecars retain every existing field and default to unchanged spacing.
  uint8_t wordSpacing = reader_defaults::WORD_SPACING;

  // Retired switches kept as zeroed padding so every later offset holds: kerning,
  // ligatures and link underline are always on now. Every field since paragraphNumberSize
  // was APPENDED LAST, and the next one must be too.
  uint8_t retiredKerning = 0;
  uint8_t retiredLigatures = 0;
  uint8_t retiredLinkUnderline = 0;

  // Retired: the time-left status bar anchors (v18). Nothing reads them; the bytes stay so
  // every later offset holds, and migrateReaderPrefsFields zeroes them so whole-blob
  // change detection never sees a stale anchor.
  uint8_t retiredChapterTime = 0;
  uint8_t retiredBookTime = 0;

  // Retired: Text Contrast, which only darkened the anti-aliased greys.
  uint8_t retiredTextContrast = 0;

  // Retired: Book Margins (always kept), Heading Page Break, Word Expansion.
  uint8_t retiredBookMargins = 0;
  uint8_t retiredHeadingPageBreak = 0;
  uint8_t retiredWordExpansion = 0;

  // Book title as its own status bar item, so a chapter Title can sit elsewhere. APPENDED LAST.
  uint8_t sbBookTitlePos = 0;  // SB_ANCHOR_OFF

  // Character Spacing in whole pixels, stored offset by reader_defaults::CHARACTER_SPACING
  // (a record older than v27 held half pixels; see migrateReaderPrefsFields).
  uint8_t characterSpacing = reader_defaults::CHARACTER_SPACING;

  // Retired: Fill Pages.
  uint8_t retiredFillPages = 0;

  // Paragraph numbers at the page edge (0) or by the first letter (1), per book like
  // paragraphNumberSize. APPENDED LAST.
  uint8_t paragraphNumberPlace = 1;  // CrossPointSettings::PARA_NUM_PLACE_TEXT

  // Finer ink weight (-2..+2 stored as 0..4, offset by reader_defaults::INK_WEIGHT).
  // Draw-only (does not affect layout or the section cache).
  uint8_t inkWeight = reader_defaults::INK_WEIGHT;

  // Keep Headings with Text (0 = off by default): never strand a heading at the page
  // bottom and avoid 1-line orphans/widows on 4+ line paragraphs. APPENDED LAST.
  uint8_t keepHeadingsWithText = 0;

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
  spec.wordSpacing = std::clamp(p.wordSpacing, reader_defaults::MIN_WORD_SPACING, reader_defaults::MAX_WORD_SPACING);
  spec.paragraphAlignment = p.paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.embeddedStyle = resolveEmbeddedStyle(p.embeddedStyle);
  // Black & White lays out exactly like Display, so switching between them keeps the
  // section cache; the reader draws the raw mode (see readerImageMode in the reader).
  spec.imageRendering = p.imageRendering == reader_defaults::IMAGES_DISPLAY_BW ? 0 : p.imageRendering;
  spec.focusReadingEnabled = p.focusReadingEnabled != 0;
  spec.guideDotsMode = resolveGuideDotsMode(p.guideDotsEnabled, p.guideDotsHidden);
  spec.paragraphIndentSpaces = std::min(p.paragraphIndentSpaces, reader_defaults::MAX_PARAGRAPH_INDENT_SPACES);
  spec.characterSpacing = static_cast<int8_t>(std::min(p.characterSpacing, reader_defaults::MAX_CHARACTER_SPACING) -
                                              reader_defaults::CHARACTER_SPACING);
  spec.keepHeadingsWithText = p.keepHeadingsWithText != 0;
  return spec;
}

// On-card size of each older blob: this struct truncated at the field the next version
// appended. They live out here because offsetof needs the completed type.
inline constexpr size_t READER_PREFS_V8_SIZE = offsetof(ReaderPrefs, paragraphNumberSize);
inline constexpr size_t READER_PREFS_V9_SIZE = offsetof(ReaderPrefs, statusBarEnabled);
inline constexpr size_t READER_PREFS_V10_SIZE = offsetof(ReaderPrefs, sbBatteryPos);
inline constexpr size_t READER_PREFS_V11_SIZE = offsetof(ReaderPrefs, retiredEmbeddedLayoutStyle);
inline constexpr size_t READER_PREFS_V12_SIZE = offsetof(ReaderPrefs, sbParaPagesPos);
inline constexpr size_t READER_PREFS_V13_SIZE = offsetof(ReaderPrefs, wordSpacing);
inline constexpr size_t READER_PREFS_V14_SIZE = offsetof(ReaderPrefs, retiredKerning);
inline constexpr size_t READER_PREFS_V15_SIZE = offsetof(ReaderPrefs, retiredLigatures);
inline constexpr size_t READER_PREFS_V16_SIZE = offsetof(ReaderPrefs, retiredLinkUnderline);
inline constexpr size_t READER_PREFS_V17_SIZE = offsetof(ReaderPrefs, retiredChapterTime);
inline constexpr size_t READER_PREFS_V18_SIZE = offsetof(ReaderPrefs, retiredTextContrast);
inline constexpr size_t READER_PREFS_V19_SIZE = offsetof(ReaderPrefs, retiredBookMargins);
inline constexpr size_t READER_PREFS_V20_SIZE = offsetof(ReaderPrefs, retiredHeadingPageBreak);
inline constexpr size_t READER_PREFS_V21_SIZE = offsetof(ReaderPrefs, retiredWordExpansion);
inline constexpr size_t READER_PREFS_V22_SIZE = offsetof(ReaderPrefs, sbBookTitlePos);
inline constexpr size_t READER_PREFS_V23_SIZE = offsetof(ReaderPrefs, characterSpacing);
inline constexpr size_t READER_PREFS_V24_SIZE = offsetof(ReaderPrefs, retiredFillPages);
inline constexpr size_t READER_PREFS_V25_SIZE = offsetof(ReaderPrefs, paragraphNumberPlace);
inline constexpr size_t READER_PREFS_V28_SIZE = offsetof(ReaderPrefs, inkWeight);
inline constexpr size_t READER_PREFS_V29_SIZE = offsetof(ReaderPrefs, keepHeadingsWithText);

// A record older than v12 carries one "Embedded Style" choice, in what is now the text
// switch. Someone who turned it off wanted the book's own styling gone, so the layout
// switch follows it rather than quietly handing back the book's margins and indents.
inline constexpr uint8_t FIRST_VERSION_WITH_SPLIT_EMBEDDED_STYLE = 12;
// A record older than v10 has no status bar on/off byte, and one older than v11 has no
// status bar layout block. Only those are seeded from the global settings on migration:
// doing it for any upgraded record would replace a per-book bar the reader configured.
inline constexpr uint8_t FIRST_VERSION_WITH_STATUS_BAR_SWITCH = 10;
inline constexpr uint8_t FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR = 11;
// v27 moved meaning without moving bytes (see ReaderPrefs::VERSION). The first record with
// a Character Spacing byte is v24.
inline constexpr uint8_t FIRST_VERSION_WITH_CHARACTER_SPACING = 24;

// The values a record older than v27 can hold that the current settings no longer offer,
// moved onto what they offer now. Shared with the preset reader so both read the same way.
// Not the retired bytes: those are zeroed by the caller once everything has read them.
inline void settleLegacyLookValues(ReaderPrefs& p) {
  p.lineSpacingPercent = std::clamp(p.lineSpacingPercent, reader_defaults::MIN_LINE_SPACING_PERCENT,
                                    reader_defaults::MAX_LINE_SPACING_PERCENT);
  p.wordSpacing = reader_defaults::snapToStep(p.wordSpacing, reader_defaults::MIN_WORD_SPACING,
                                              reader_defaults::MAX_WORD_SPACING, reader_defaults::WORD_SPACING_STEP);
  // The side margin was 0 to 100. Only All Sides follows it onto the other sides; the
  // vertical margins of the other modes keep their own 0 to 100 range.
  p.screenMargin = reader_defaults::snapToStep(p.screenMargin, reader_defaults::MIN_SCREEN_MARGIN,
                                               reader_defaults::MAX_SCREEN_MARGIN, reader_defaults::SCREEN_MARGIN_STEP);
  if (margin_link::toMode(p.marginLinkMode) == margin_link::Mode::AllSides) {
    p.screenMarginTop = p.screenMarginBottom = p.screenMargin;
    p.dynamicMargins = 0;
  }
  // Placeholder and Suppress were hard-set to Display from 2026-08-11, so a stored value
  // was inert. Now that the row is back it would hide images the reader has been seeing.
  p.imageRendering = 0;
}

// `layoutStyleAlreadyRead` is set only for the interim v11 layout, whose record really
// does carry its own layout switch: seeding it from the text switch there would throw
// away a choice the reader made.
inline void migrateReaderPrefsFields(const uint8_t version, ReaderPrefs& p, const bool layoutStyleAlreadyRead = false) {
  p.reserved14 = 0;  // Canonical padding for whole-blob change detection.
  p.retiredChapterTime = 0;
  p.retiredBookTime = 0;
  p.retiredTextAntiAliasing = 0;
  p.retiredTextContrast = 0;
  if (version == 5) p.fontPointSize = foldLegacyReaderFontSize(p.fontPointSize);
  if (!layoutStyleAlreadyRead && version < FIRST_VERSION_WITH_SPLIT_EMBEDDED_STYLE) {
    p.retiredEmbeddedLayoutStyle = p.embeddedStyle;
  }
  // Before v9 the whole reading-defaults block is re-seeded, the indent included, so the
  // legacy indent bytes below are only read from v9 on.
  if (version < ReaderPrefs::FIRST_VERSION_WITH_CURRENT_DEFAULTS) p.adoptCurrentReadingDefaults();
  if (version < 27) {
    if (version >= ReaderPrefs::FIRST_VERSION_WITH_CURRENT_DEFAULTS) {
      p.paragraphIndentSpaces =
          reader_defaults::indentSpacesFromLegacy(p.retiredFirstLineIndentMode, p.paragraphIndentSpaces);
    }
    // One switch now: off if the reader turned either half off (off meant "the book's own
    // styling gone", so it must not come back through the other half).
    p.embeddedStyle = p.embeddedStyle && p.retiredEmbeddedLayoutStyle;
    if (version >= FIRST_VERSION_WITH_CHARACTER_SPACING) {
      p.characterSpacing = reader_defaults::characterSpacingFromLegacy(p.characterSpacing);
    }
    settleLegacyLookValues(p);
  }
  // v28 turns embeddedStyle into 3-way Off (0) / Text Only (1) / Full (2); legacy 1 meant Full.
  if (version < 28 && p.embeddedStyle != EMBEDDED_STYLE_OFF) {
    p.embeddedStyle = EMBEDDED_STYLE_FULL;
  }
  // The retired bytes carry nothing now; zero them so whole-blob comparison is exact.
  p.retiredParagraphSpacing = p.retiredFirstLineIndentMode = p.retiredEmbeddedLayoutStyle = 0;
  p.retiredKerning = p.retiredLigatures = p.retiredLinkUnderline = p.retiredBookMargins = 0;
  p.retiredHeadingPageBreak = p.retiredWordExpansion = p.retiredFillPages = 0;
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
  // The byte sat where retiredTextAntiAliasing now begins, directly after embeddedTextStyle.
  constexpr size_t split = offsetof(ReaderPrefs, retiredTextAntiAliasing);
  auto* raw = reinterpret_cast<uint8_t*>(&out);
  std::memcpy(raw, record, split);
  std::memcpy(raw + split, record + split + 1, READER_PREFS_V11_SIZE - split);
  out.retiredEmbeddedLayoutStyle = record[split];
}

inline constexpr size_t readerPrefsRecordSize(const uint8_t version) {
  if (version >= 5 && version <= 8) return READER_PREFS_V8_SIZE;
  if (version == 9) return READER_PREFS_V9_SIZE;
  if (version == 10) return READER_PREFS_V10_SIZE;
  if (version == 11) return READER_PREFS_V11_SIZE;
  if (version == 12) return READER_PREFS_V12_SIZE;
  if (version == 13) return READER_PREFS_V13_SIZE;
  if (version == 14) return READER_PREFS_V14_SIZE;
  if (version == 15) return READER_PREFS_V15_SIZE;
  if (version == 16) return READER_PREFS_V16_SIZE;
  if (version == 17) return READER_PREFS_V17_SIZE;
  if (version == 18) return READER_PREFS_V18_SIZE;
  if (version == 19) return READER_PREFS_V19_SIZE;
  if (version == 20) return READER_PREFS_V20_SIZE;
  if (version == 21) return READER_PREFS_V21_SIZE;
  if (version == 22) return READER_PREFS_V22_SIZE;
  if (version == 23) return READER_PREFS_V23_SIZE;
  if (version == 24) return READER_PREFS_V24_SIZE;
  if (version == 25) return READER_PREFS_V25_SIZE;
  // v26, v27 and v28 share one byte layout: v27/v28 only changed what existing bytes mean.
  if (version == 26 || version == 27 || version == 28) return READER_PREFS_V28_SIZE;
  if (version == 29) return READER_PREFS_V29_SIZE;
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
static_assert(READER_PREFS_V14_SIZE == READER_PREFS_V13_SIZE + 1, "v14 adds wordSpacing");
static_assert(READER_PREFS_V15_SIZE == READER_PREFS_V14_SIZE + 1, "v15 adds kerning");
static_assert(READER_PREFS_V16_SIZE == READER_PREFS_V15_SIZE + 1, "v16 adds ligatures");
static_assert(READER_PREFS_V17_SIZE == READER_PREFS_V16_SIZE + 1, "v17 adds linkUnderline");
static_assert(READER_PREFS_V18_SIZE == READER_PREFS_V17_SIZE + 2, "v18 adds the two time-left anchors (retired since)");
static_assert(READER_PREFS_V19_SIZE == READER_PREFS_V18_SIZE + 1, "v19 adds textContrast (retired since)");
static_assert(READER_PREFS_V20_SIZE == READER_PREFS_V19_SIZE + 1, "v20 adds bookMargins");
static_assert(READER_PREFS_V21_SIZE == READER_PREFS_V20_SIZE + 1, "v21 adds headingPageBreak");
static_assert(READER_PREFS_V22_SIZE == READER_PREFS_V21_SIZE + 1, "v22 adds wordExpansion (retired in v27)");
static_assert(READER_PREFS_V23_SIZE == READER_PREFS_V22_SIZE + 1, "v23 adds sbBookTitlePos");
static_assert(READER_PREFS_V24_SIZE == READER_PREFS_V23_SIZE + 1, "v24 adds characterSpacing");
static_assert(READER_PREFS_V25_SIZE == READER_PREFS_V24_SIZE + 1, "v25 adds fillPages");
static_assert(READER_PREFS_V28_SIZE == READER_PREFS_V25_SIZE + 1, "v26 to v28 add paragraphNumberPlace");
static_assert(READER_PREFS_V29_SIZE == READER_PREFS_V28_SIZE + 1, "v29 adds inkWeight");
static_assert(sizeof(ReaderPrefs) == READER_PREFS_V29_SIZE + 1,
              "keepHeadingsWithText must be the last byte: every new field goes last, or "
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

// The retired bytes are padding that keeps every later offset; migrateReaderPrefsFields zeroes them.
inline constexpr size_t READER_PREFS_RETIRED_BYTES = 14;
static_assert(reader_look::SCREEN_FIELD_COUNT + reader_look::BOOK_FIELD_COUNT + reader_look::STATUS_BAR_FIELD_COUNT +
                      sizeof(ReaderPrefs::sdFontFamilyName) + sizeof(ReaderPrefs::reserved14) +
                      READER_PREFS_RETIRED_BYTES ==
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
  uint8_t bookTitlePos = 0;
  uint8_t bookBar = 0;
  uint8_t chapterBar = 0;
  uint8_t barThickness = 0;
  uint8_t floatingBar = 0;  // retired option, no longer read
  uint8_t barOutline = 0;   // retired option, no longer read
  uint8_t offBar = 0;

  bool textOn() const { return enabled != 0; }

  // ── Titles ─────────────────────────────────────────────────────────────────
  // Title Source is retired: Book was the Book Title item before it existed, so an old
  // block that chose it is folded onto that item. Every reader of the block goes through
  // statusBar() / statusBarOf(), which fold, so nothing downstream reads titleSource.
  void foldTitleSource() {
    if (titleSource != 0) return;  // SB_TITLE_BOOK
    if (bookTitlePos == 0) bookTitlePos = titlePos;
    titlePos = 0;
    titleSource = 1;  // SB_TITLE_CHAPTER
  }
  // The bar has one title slot that wraps and that its neighbours reflow around. It holds
  // the chapter title, with the book title on the line above when both are on the same
  // edge (top or bottom), at the chapter title's anchor: side by side, a wrapping chapter
  // title would push the book title off the bar. On opposite edges a placed book title is
  // a one-line item of its own (bookItem). A chapterless
  // book (TXT, flat XTC) has no chapter title: the slot then holds the book title, at
  // the Book Title anchor if that is placed, else at the Chapter Title anchor.
  struct TitleSlot {
    uint8_t anchor = 0;      // 0 = no title slot
    bool chapter = false;    // slot holds the chapter title (else the book title)
    bool bookAbove = false;  // book title stacked on the line above the chapter title (same edge)
    uint8_t bookItem = 0;    // anchor of the separate one-line book title, 0 = none
  };
  TitleSlot titleSlot(bool hasChapters) const {
    TitleSlot t;
    if (titlePos != 0 && hasChapters) {
      t.anchor = titlePos;
      t.chapter = true;
      t.bookAbove = bookTitlePos != 0 && (bookTitlePos <= 3) == (titlePos <= 3);  // 1..3 top, 4..6 bottom
      if (!t.bookAbove) t.bookItem = bookTitlePos;
    } else {
      t.anchor = bookTitlePos != 0 ? bookTitlePos : titlePos;
    }
    return t;
  }
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
  b.foldTitleSource();
  return b;
}
// Calm mode overlay: hides the status bar and off-bar without modifying stored prefs or themes.
inline StatusBarBlock effectiveStatusBar(StatusBarBlock b, const bool calmMode) {
  if (calmMode) {
    b.enabled = 0;
    b.offBar = StatusBarBlock::OFF_BAR_OFF;
  }
  return b;
}
inline void setStatusBarOf(ReaderPrefs& p, const StatusBarBlock& b) {
#define CP_SB_TO_PREFS(prefsName, settingsName, blockName) p.prefsName = b.blockName;
  READER_STATUS_BAR_FIELDS(CP_SB_TO_PREFS)
#undef CP_SB_TO_PREFS
}

// ── A book's look on open ─────────────────────────────────────────────────────
// Whether the book's stylesheet has to be parsed. Decided from the book's own look:
// a book with its Book Styling switch on needs its CSS even when the global switch is
// off, and the loader never builds the CSS cache for a book that skipped it.
inline bool wantsBookCss(const ReaderPrefs& p) { return p.embeddedStyle != EMBEDDED_STYLE_OFF; }

// Brings a sidecar record read at `fromVersion` in line with this firmware.
// - A record written before whole-book numbering was removed can still say 2; the
//   Settings row and the menu cycle can no longer produce it.
// - The status bar layout is the reader's, not the book's, so it always comes from the
//   global look: otherwise Settings > Customise Status Bar never reaches a book with its
//   own look. The show/hide switch and the hidden-bar progress bar stay the book's own,
//   like the quick toggle that sets them. A record older than v11 stops before the block
//   and takes all of it from global.
inline void settleSidecarPrefs(ReaderPrefs& p, const uint8_t fromVersion, const ReaderPrefs& global) {
  if (p.paragraphNumbering >= reader_defaults::PARAGRAPH_NUMBERING_COUNT) {
    p.paragraphNumbering = reader_defaults::PARAGRAPH_NUMBERING;
  }
  const uint8_t enabled = p.statusBarEnabled;
  const uint8_t offBar = p.sbOffBar;
  p.adoptStatusBarFrom(global);
  if (fromVersion < FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR) return;
  p.statusBarEnabled = enabled;
  p.sbOffBar = offBar;
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
