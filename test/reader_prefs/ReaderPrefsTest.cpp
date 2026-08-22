// Host tests for the per-book ReaderPrefs snapshot: the [version][POD] stream
// serialization and its guards. fromGlobal() and the HalFile overloads depend on
// the CrossPointSettings singleton / SD stack (not host-buildable), so they are
// covered on-device, not here — this file locks down the format logic.
#include <gtest/gtest.h>

#include <cstring>
#include <sstream>

#include "ReaderPrefs.h"

namespace {

// A fully non-default snapshot so a round-trip that drops or reorders any field fails.
ReaderPrefs makeSample() {
  ReaderPrefs p;
  p.fontFamily = 1;
  p.fontPointSize = 18;
  p.lineSpacingPercent = 120;
  p.paragraphAlignment = 4;
  p.extraParagraphSpacing = 0;
  p.paragraphSpacing = 80;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.verticalMarginsLinked = 0;
  p.dynamicMargins = 2;
  p.focusReadingEnabled = 1;
  p.guideDotsEnabled = 1;
  p.guideDotsHidden = 1;
  p.hyphenationEnabled = 1;
  p.embeddedTextStyle = 0;
  p.embeddedLayoutStyle = 0;
  p.textAntiAliasing = 0;
  p.imageRendering = 2;
  p.paragraphNumbering = 2;  // whole book
  p.paperbackLookBody = 0;
  p.paperbackLookStatus = 1;
  p.firstLineIndentMode = 1;  // Custom %
  p.firstLineIndentPercent = 40;
  p.paragraphNumberSize = 0;  // Small
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, "Bookerly", sizeof(p.sdFontFamilyName) - 1);
  return p;
}

void expectEqual(const ReaderPrefs& a, const ReaderPrefs& b) {
  EXPECT_EQ(a.fontFamily, b.fontFamily);
  EXPECT_EQ(a.fontPointSize, b.fontPointSize);
  EXPECT_EQ(a.lineSpacingPercent, b.lineSpacingPercent);
  EXPECT_EQ(a.paragraphAlignment, b.paragraphAlignment);
  EXPECT_EQ(a.extraParagraphSpacing, b.extraParagraphSpacing);
  EXPECT_EQ(a.paragraphSpacing, b.paragraphSpacing);
  EXPECT_EQ(a.screenMargin, b.screenMargin);
  EXPECT_EQ(a.screenMarginTop, b.screenMarginTop);
  EXPECT_EQ(a.screenMarginBottom, b.screenMarginBottom);
  EXPECT_EQ(a.verticalMarginsLinked, b.verticalMarginsLinked);
  EXPECT_EQ(a.dynamicMargins, b.dynamicMargins);
  EXPECT_EQ(a.focusReadingEnabled, b.focusReadingEnabled);
  EXPECT_EQ(a.guideDotsEnabled, b.guideDotsEnabled);
  EXPECT_EQ(a.guideDotsHidden, b.guideDotsHidden);
  EXPECT_EQ(a.hyphenationEnabled, b.hyphenationEnabled);
  EXPECT_EQ(a.embeddedTextStyle, b.embeddedTextStyle);
  EXPECT_EQ(a.embeddedLayoutStyle, b.embeddedLayoutStyle);
  EXPECT_EQ(a.textAntiAliasing, b.textAntiAliasing);
  EXPECT_EQ(a.imageRendering, b.imageRendering);
  EXPECT_EQ(a.paragraphNumbering, b.paragraphNumbering);
  EXPECT_EQ(a.paperbackLookBody, b.paperbackLookBody);
  EXPECT_EQ(a.paperbackLookStatus, b.paperbackLookStatus);
  EXPECT_EQ(a.firstLineIndentMode, b.firstLineIndentMode);
  EXPECT_EQ(a.firstLineIndentPercent, b.firstLineIndentPercent);
  EXPECT_EQ(a.paragraphNumberSize, b.paragraphNumberSize);
  EXPECT_STREQ(a.sdFontFamilyName, b.sdFontFamilyName);
  // POD change-detection is a whole-blob memcmp, so the bytes must match exactly.
  EXPECT_EQ(0, std::memcmp(&a, &b, sizeof(ReaderPrefs)));
}

}  // namespace

TEST(ReaderPrefs, ParagraphNumberingDefaultsOff) {
  ReaderPrefs p;
  EXPECT_EQ(0, p.paragraphNumbering);
}

TEST(ReaderPrefs, FirstLineIndentDefaultsToBook) {
  ReaderPrefs p;
  EXPECT_EQ(0, p.firstLineIndentMode);  // Book (respect CSS)
  EXPECT_EQ(0, p.firstLineIndentPercent);
}

TEST(ReaderPrefs, StreamRoundTrip) {
  const ReaderPrefs original = makeSample();
  std::stringstream ss;
  writeReaderPrefs(ss, original);

  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(ss, loaded));
  expectEqual(original, loaded);
}

TEST(ReaderPrefs, LegacyFontSizeSlotFolds) {
  EXPECT_EQ(12, foldLegacyReaderFontSize(0));  // was SMALL
  EXPECT_EQ(14, foldLegacyReaderFontSize(1));  // was MEDIUM
  EXPECT_EQ(16, foldLegacyReaderFontSize(2));  // was LARGE
  EXPECT_EQ(18, foldLegacyReaderFontSize(3));  // was EXTRA_LARGE
  // Anything already a point size is left alone.
  EXPECT_EQ(12, foldLegacyReaderFontSize(12));
  EXPECT_EQ(22, foldLegacyReaderFontSize(22));
}

// A v5 sidecar has the same layout; only fontPointSize's meaning changed. It must
// be read and folded, not discarded — discarding silently drops every per-book
// override the first time this build runs.
TEST(ReaderPrefs, Version5SidecarIsFoldedNotDropped) {
  ReaderPrefs legacy = makeSample();
  legacy.fontPointSize = 2;  // old LARGE slot
  std::stringstream ss;
  const uint8_t v5 = 5;
  ss.write(reinterpret_cast<const char*>(&v5), 1);
  // A v5 record on the card is this struct WITHOUT the byte v9 appended.
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V8_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_EQ(16, loaded.fontPointSize);
  // A v5 sidecar predates the v7 reading defaults too, so it is upgraded on the way in.
  EXPECT_TRUE(migrated);
  // Every field outside the fold and the reading defaults survives untouched.
  EXPECT_EQ(legacy.fontFamily, loaded.fontFamily);
  EXPECT_EQ(legacy.lineSpacingPercent, loaded.lineSpacingPercent);
  EXPECT_EQ(legacy.screenMargin, loaded.screenMargin);
  EXPECT_STREQ(legacy.sdFontFamilyName, loaded.sdFontFamilyName);
}

// A v6 sidecar is the common case on an upgrading device: same layout, but written
// before the reading defaults changed. It must be upgraded in place, never dropped —
// dropping it would discard every per-book setting the user ever chose.
TEST(ReaderPrefs, Version6SidecarAdoptsNewReadingDefaultsAndKeepsTheRest) {
  ReaderPrefs legacy = makeSample();
  legacy.paragraphSpacing = 0;
  legacy.firstLineIndentMode = 0;
  legacy.firstLineIndentPercent = 0;
  legacy.paragraphNumbering = 0;
  legacy.fontPointSize = 18;
  legacy.screenMargin = 35;
  std::stringstream ss;
  const uint8_t v6 = 6;
  ss.write(reinterpret_cast<const char*>(&v6), 1);
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V8_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(reader_defaults::PARAGRAPH_SPACING_PERCENT, loaded.paragraphSpacing);
  EXPECT_EQ(reader_defaults::FIRST_LINE_INDENT_MODE, loaded.firstLineIndentMode);
  EXPECT_EQ(reader_defaults::FIRST_LINE_INDENT_PERCENT, loaded.firstLineIndentPercent);
  EXPECT_EQ(reader_defaults::PARAGRAPH_NUMBERING, loaded.paragraphNumbering);
  // A v6 point size is already a point size, so it is not folded.
  EXPECT_EQ(18, loaded.fontPointSize);
  // Everything the user chose that is not one of the four defaults is left alone.
  EXPECT_EQ(35, loaded.screenMargin);
  EXPECT_EQ(legacy.lineSpacingPercent, loaded.lineSpacingPercent);
  EXPECT_EQ(legacy.paragraphAlignment, loaded.paragraphAlignment);
  EXPECT_STREQ(legacy.sdFontFamilyName, loaded.sdFontFamilyName);
}

// A current sidecar is read as-is. Nothing is re-seeded, so a user who deliberately
// turned the indent back off keeps it off across every reopen.
TEST(ReaderPrefs, CurrentVersionIsNotMigrated) {
  ReaderPrefs current = makeSample();
  current.paragraphSpacing = 0;
  current.firstLineIndentMode = 0;
  current.firstLineIndentPercent = 0;
  current.paragraphNumbering = 0;
  std::stringstream ss;
  const uint8_t ver = ReaderPrefs::VERSION;
  ss.write(reinterpret_cast<const char*>(&ver), 1);
  ss.write(reinterpret_cast<const char*>(&current), sizeof(ReaderPrefs));

  ReaderPrefs loaded;
  bool migrated = true;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_FALSE(migrated);
  EXPECT_EQ(0, loaded.paragraphSpacing);
  EXPECT_EQ(0, loaded.firstLineIndentMode);
  EXPECT_EQ(0, loaded.firstLineIndentPercent);
  EXPECT_EQ(0, loaded.paragraphNumbering);
}

TEST(ReaderPrefs, WrongVersionRejected) {
  const ReaderPrefs sample = makeSample();
  std::stringstream ss;
  const uint8_t badVersion = ReaderPrefs::VERSION + 1;
  ss.write(reinterpret_cast<const char*>(&badVersion), 1);
  ss.write(reinterpret_cast<const char*>(&sample), sizeof(ReaderPrefs));

  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

TEST(ReaderPrefs, TruncatedRejected) {
  // Version byte present, POD payload missing entirely.
  std::stringstream ss;
  const uint8_t version = ReaderPrefs::VERSION;
  ss.write(reinterpret_cast<const char*>(&version), 1);

  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

TEST(ReaderPrefs, EmptyRejected) {
  std::stringstream ss;
  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

// ── v9: the paragraph-number size ─────────────────────────────────────────────
// v9 is the first version since v5 to change the LAYOUT rather than just meanings:
// one byte appended at the end. These tests pin the two things that could go wrong —
// reading an older record at the wrong length (which would drop every per-book
// setting the user ever chose) and failing to re-seed the new default onto it.

TEST(ReaderPrefs, ParagraphNumberSizeDefaultsToDouble) {
  const ReaderPrefs p;
  EXPECT_EQ(1, p.paragraphNumberSize);  // PARA_NUM_SIZE_DOUBLE
}

TEST(ReaderPrefs, EachOlderRecordIsAStrictPrefixOfTheCurrentOne) {
  EXPECT_EQ(offsetof(ReaderPrefs, paragraphNumberSize), READER_PREFS_V8_SIZE);
  EXPECT_EQ(offsetof(ReaderPrefs, statusBarEnabled), READER_PREFS_V9_SIZE);
  EXPECT_EQ(offsetof(ReaderPrefs, sbBatteryPos), READER_PREFS_V10_SIZE);
  EXPECT_EQ(READER_PREFS_V8_SIZE + 1, READER_PREFS_V9_SIZE);
  EXPECT_EQ(READER_PREFS_V9_SIZE + 1, READER_PREFS_V10_SIZE);
  EXPECT_LT(READER_PREFS_V10_SIZE, sizeof(ReaderPrefs));
}

// The one rule both readers share. Getting a size wrong here does not fail loudly: it
// reads a record at the wrong length and silently drops per-book settings.
TEST(ReaderPrefs, RecordSizeIsKnownForEveryReadableVersionAndZeroOtherwise) {
  for (uint8_t v = 5; v <= 8; v++) EXPECT_EQ(READER_PREFS_V8_SIZE, readerPrefsRecordSize(v));
  EXPECT_EQ(READER_PREFS_V9_SIZE, readerPrefsRecordSize(9));
  EXPECT_EQ(READER_PREFS_V10_SIZE, readerPrefsRecordSize(10));
  EXPECT_EQ(sizeof(ReaderPrefs), readerPrefsRecordSize(ReaderPrefs::VERSION));
  EXPECT_EQ(0u, readerPrefsRecordSize(4));
  EXPECT_EQ(0u, readerPrefsRecordSize(0));
  EXPECT_EQ(0u, readerPrefsRecordSize(ReaderPrefs::VERSION + 1));
}

// The status bar switch is per-book. A v9 sidecar predates it and stops one byte short,
// so the field must fall back to its default instead of eating whatever followed.
TEST(ReaderPrefs, Version9SidecarKeepsEverythingAndDefaultsTheStatusBar) {
  ReaderPrefs legacy = makeSample();
  legacy.paragraphNumberSize = 0;  // Small, a real v9 user choice that must survive
  legacy.statusBarEnabled = 0;     // never stored by a v9 writer; must not survive the read
  std::stringstream ss;
  const uint8_t v9 = 9;
  ss.write(reinterpret_cast<const char*>(&v9), 1);
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V9_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(1, loaded.statusBarEnabled);
  EXPECT_EQ(0, loaded.paragraphNumberSize);
  EXPECT_EQ(legacy.fontFamily, loaded.fontFamily);
  EXPECT_EQ(legacy.fontPointSize, loaded.fontPointSize);
  EXPECT_EQ(legacy.screenMargin, loaded.screenMargin);
  EXPECT_EQ(legacy.paperbackLookBody, loaded.paperbackLookBody);
  EXPECT_EQ(legacy.paperbackLookStatus, loaded.paperbackLookStatus);
  EXPECT_STREQ(legacy.sdFontFamilyName, loaded.sdFontFamilyName);
}

TEST(ReaderPrefs, CurrentVersionCarriesTheStatusBarByte) {
  ReaderPrefs sample = makeSample();
  sample.statusBarEnabled = 0;  // hidden for this book only
  std::stringstream ss;
  writeReaderPrefs(ss, sample);

  ReaderPrefs loaded;
  bool migrated = true;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_FALSE(migrated);
  EXPECT_EQ(0, loaded.statusBarEnabled);
}

// The status bar switch is an in-menu per-book toggle, so the Reader Settings overlay
// never carries it. The book's own value must survive an edit of unrelated rows.
TEST(ReaderPrefs, StatusBarSwitchSurvivesAReaderSettingsEdit) {
  ReaderPrefs book = makeSample();
  book.statusBarEnabled = 0;
  ReaderPrefs live = book;
  live.statusBarEnabled = 1;  // the GLOBAL value, which is what the overlay leaves behind
  live.screenMargin = static_cast<uint8_t>(book.screenMargin + 5);

  const auto decision = decideReaderOverride(live, book, true);
  EXPECT_EQ(ReaderOverrideAction::Write, decision.action);
  EXPECT_EQ(0, decision.prefs.statusBarEnabled);
  EXPECT_EQ(live.screenMargin, decision.prefs.screenMargin);
}

TEST(ReaderPrefs, Version8SidecarKeepsEverythingAndAdoptsDoubleSize) {
  ReaderPrefs legacy = makeSample();
  legacy.paragraphNumbering = 2;   // whole book, a real user choice
  legacy.paragraphNumberSize = 0;  // never stored by a v8 writer; must not survive the read
  std::stringstream ss;
  const uint8_t v8 = 8;
  ss.write(reinterpret_cast<const char*>(&v8), 1);
  // A v8 file holds no size byte at all — it stops one byte short.
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V8_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_TRUE(migrated);
  // The new field takes the current default rather than whatever byte followed.
  EXPECT_EQ(reader_defaults::PARAGRAPH_NUMBER_SIZE, loaded.paragraphNumberSize);
  // Everything the user chose that is not a re-seeded default survives untouched.
  EXPECT_EQ(legacy.fontFamily, loaded.fontFamily);
  EXPECT_EQ(legacy.fontPointSize, loaded.fontPointSize);
  EXPECT_EQ(legacy.lineSpacingPercent, loaded.lineSpacingPercent);
  EXPECT_EQ(legacy.screenMargin, loaded.screenMargin);
  EXPECT_EQ(legacy.screenMarginTop, loaded.screenMarginTop);
  EXPECT_EQ(legacy.screenMarginBottom, loaded.screenMarginBottom);
  EXPECT_EQ(legacy.dynamicMargins, loaded.dynamicMargins);
  EXPECT_EQ(legacy.paperbackLookBody, loaded.paperbackLookBody);
  EXPECT_STREQ(legacy.sdFontFamilyName, loaded.sdFontFamilyName);
}

TEST(ReaderPrefs, Version8SidecarTruncatedMidRecordIsRejected) {
  const ReaderPrefs legacy = makeSample();
  std::stringstream ss;
  const uint8_t v8 = 8;
  ss.write(reinterpret_cast<const char*>(&v8), 1);
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V8_SIZE - 1);

  ReaderPrefs loaded;
  EXPECT_FALSE(readReaderPrefs(ss, loaded));
}

TEST(ReaderPrefs, CurrentVersionCarriesTheSizeByte) {
  ReaderPrefs sample = makeSample();
  sample.paragraphNumberSize = 0;  // Small, chosen per book
  std::stringstream ss;
  writeReaderPrefs(ss, sample);

  ReaderPrefs loaded;
  bool migrated = true;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_FALSE(migrated);
  EXPECT_EQ(0, loaded.paragraphNumberSize);
  expectEqual(sample, loaded);
}

// ── Mid-edit override decision ────────────────────────────────────────────────
// While the in-book Reader Settings screen is open, every row change must already
// be safe on the card, so switching the reader off inside that screen keeps it.
// decideReaderOverride() is the pure rule behind that write.

TEST(ReaderOverrideDecision, ChangedValueIsWritten) {
  ReaderPrefs book;
  ReaderPrefs live = book;
  live.fontPointSize = 18;

  const auto decision = decideReaderOverride(live, book, /*bookIsCustom=*/false);
  EXPECT_EQ(ReaderOverrideAction::Write, decision.action);
  EXPECT_EQ(18, decision.prefs.fontPointSize);
}

TEST(ReaderOverrideDecision, InMenuTogglesComeFromTheBookNotTheOverlay) {
  // paragraphNumbering and the two Paperback flags are in-menu per-book toggles.
  // The overlay does not carry them, so the live snapshot holds the GLOBAL values;
  // the book's own must survive an unrelated font edit.
  ReaderPrefs book;
  book.paragraphNumbering = 2;
  book.paperbackLookBody = 0;
  book.paperbackLookStatus = 0;

  ReaderPrefs live = book;
  live.paragraphNumbering = 0;  // global values leaking through fromGlobal()
  live.paperbackLookBody = 1;
  live.paperbackLookStatus = 1;
  live.screenMargin = 30;  // the actual edit

  const auto decision = decideReaderOverride(live, book, /*bookIsCustom=*/true);
  EXPECT_EQ(ReaderOverrideAction::Write, decision.action);
  EXPECT_EQ(30, decision.prefs.screenMargin);
  EXPECT_EQ(2, decision.prefs.paragraphNumbering);
  EXPECT_EQ(0, decision.prefs.paperbackLookBody);
  EXPECT_EQ(0, decision.prefs.paperbackLookStatus);
}

TEST(ReaderOverrideDecision, ParagraphNumberSizeComesFromTheBookNotTheOverlay) {
  // Size is an in-book menu choice, so the Reader Settings overlay carries the GLOBAL
  // value for it. Editing a font row must not drag that global size onto the book.
  ReaderPrefs book = makeSample();
  book.paragraphNumberSize = 0;  // this book was set to Small
  ReaderPrefs live = book;
  live.paragraphNumberSize = 1;  // global says Double
  live.fontPointSize = 16;       // the row actually edited

  const auto decision = decideReaderOverride(live, book, true);
  EXPECT_EQ(ReaderOverrideAction::Write, decision.action);
  EXPECT_EQ(16, decision.prefs.fontPointSize);
  EXPECT_EQ(0, decision.prefs.paragraphNumberSize);
}

TEST(ReaderOverrideDecision, NoChangeOnACustomBookKeepsItsFile) {
  ReaderPrefs book = makeSample();
  const auto decision = decideReaderOverride(book, book, /*bookIsCustom=*/true);
  EXPECT_EQ(ReaderOverrideAction::Keep, decision.action);
}

TEST(ReaderOverrideDecision, EditUndoneOnAGlobalBookRemovesTheFile) {
  // Changed a row, then changed it back. The book was following global, so the
  // sidecar written mid-edit must go again rather than freeze it as custom.
  ReaderPrefs book;
  const auto decision = decideReaderOverride(book, book, /*bookIsCustom=*/false);
  EXPECT_EQ(ReaderOverrideAction::Remove, decision.action);
}

TEST(ReaderOverrideDecision, SdFontNameChangeIsWritten) {
  ReaderPrefs book;
  ReaderPrefs live = book;
  std::strncpy(live.sdFontFamilyName, "Bookerly", sizeof(live.sdFontFamilyName) - 1);

  const auto decision = decideReaderOverride(live, book, /*bookIsCustom=*/true);
  EXPECT_EQ(ReaderOverrideAction::Write, decision.action);
  EXPECT_STREQ("Bookerly", decision.prefs.sdFontFamilyName);
}

// The status bar's WHERE went per-book in v11. A v10 sidecar stops before that block,
// so every anchor must fall back to its default rather than eating whatever followed —
// a misread here would park the battery or the title at a position the user never chose.
TEST(ReaderPrefs, Version10SidecarKeepsEverythingAndDefaultsTheStatusBarLayout) {
  ReaderPrefs legacy = makeSample();
  legacy.statusBarEnabled = 0;  // a real v10 user choice that must survive
  std::stringstream ss;
  const uint8_t v10 = 10;
  ss.write(reinterpret_cast<const char*>(&v10), 1);
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V10_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(0, loaded.statusBarEnabled);

  const ReaderPrefs fresh;
  EXPECT_EQ(fresh.sbBatteryPos, loaded.sbBatteryPos);
  EXPECT_EQ(fresh.sbTitlePos, loaded.sbTitlePos);
  EXPECT_EQ(fresh.sbPagePos, loaded.sbPagePos);
  EXPECT_EQ(fresh.sbBookBar, loaded.sbBookBar);
  EXPECT_EQ(fresh.sbBarThickness, loaded.sbBarThickness);
  EXPECT_EQ(fresh.sbOffBar, loaded.sbOffBar);
}

// Migration must not hand an old book this firmware's shipped bar. The block is
// seeded from the settings the user already sees, or opening a book written before
// v11 silently rearranges its status bar.
TEST(ReaderPrefs, AdoptStatusBarFromCopiesTheWholeBlockAndNothingElse) {
  ReaderPrefs global;
  global.statusBarEnabled = 0;
  global.sbBatteryPos = 3;
  global.sbTitlePos = 1;
  global.sbPagePos = 2;
  global.sbBookBar = 2;
  global.sbBarThickness = 2;
  global.sbFloatingBar = 1;
  global.sbOffBar = 3;

  ReaderPrefs book = makeSample();
  const uint8_t keptFontSize = book.fontPointSize;
  const uint8_t keptMargin = book.screenMargin;
  book.adoptStatusBarFrom(global);

  EXPECT_EQ(0, book.statusBarEnabled);
  EXPECT_EQ(3, book.sbBatteryPos);
  EXPECT_EQ(1, book.sbTitlePos);
  EXPECT_EQ(2, book.sbPagePos);
  EXPECT_EQ(2, book.sbBookBar);
  EXPECT_EQ(2, book.sbBarThickness);
  EXPECT_EQ(1, book.sbFloatingBar);
  EXPECT_EQ(3, book.sbOffBar);
  // The book's own reading settings are untouched: this copies the bar, nothing else.
  EXPECT_EQ(keptFontSize, book.fontPointSize);
  EXPECT_EQ(keptMargin, book.screenMargin);
}
