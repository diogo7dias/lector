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
  p.wordSpacing = 125;
  p.screenMargin = 35;
  p.screenMarginTop = 12;
  p.screenMarginBottom = 18;
  p.marginLinkMode = 0;
  p.dynamicMargins = 2;
  p.focusReadingEnabled = 1;
  p.guideDotsEnabled = 1;
  p.guideDotsHidden = 1;
  p.embeddedStyle = 0;
  p.retiredTextAntiAliasing = 0;
  p.imageRendering = 2;
  p.paragraphNumbering = 2;  // whole book
  p.paperbackLookBody = 0;
  p.paperbackLookStatus = 1;
  p.paragraphIndentSpaces = 4;
  p.characterSpacing = reader_defaults::CHARACTER_SPACING + 1;
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
  EXPECT_EQ(a.wordSpacing, b.wordSpacing);
  EXPECT_EQ(a.characterSpacing, b.characterSpacing);
  EXPECT_EQ(a.retiredChapterTime, b.retiredChapterTime);
  EXPECT_EQ(a.retiredBookTime, b.retiredBookTime);
  EXPECT_EQ(a.retiredTextContrast, b.retiredTextContrast);
  EXPECT_EQ(a.paragraphNumberPlace, b.paragraphNumberPlace);
  EXPECT_EQ(a.sbBookTitlePos, b.sbBookTitlePos);
  EXPECT_EQ(a.screenMargin, b.screenMargin);
  EXPECT_EQ(a.screenMarginTop, b.screenMarginTop);
  EXPECT_EQ(a.screenMarginBottom, b.screenMarginBottom);
  EXPECT_EQ(a.marginLinkMode, b.marginLinkMode);
  EXPECT_EQ(a.dynamicMargins, b.dynamicMargins);
  EXPECT_EQ(a.focusReadingEnabled, b.focusReadingEnabled);
  EXPECT_EQ(a.guideDotsEnabled, b.guideDotsEnabled);
  EXPECT_EQ(a.guideDotsHidden, b.guideDotsHidden);
  EXPECT_EQ(a.embeddedStyle, b.embeddedStyle);
  EXPECT_EQ(a.retiredTextAntiAliasing, b.retiredTextAntiAliasing);
  EXPECT_EQ(a.imageRendering, b.imageRendering);
  EXPECT_EQ(a.paragraphNumbering, b.paragraphNumbering);
  EXPECT_EQ(a.paperbackLookBody, b.paperbackLookBody);
  EXPECT_EQ(a.paperbackLookStatus, b.paperbackLookStatus);
  EXPECT_EQ(a.paragraphIndentSpaces, b.paragraphIndentSpaces);
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

TEST(ReaderPrefs, ParagraphIndentDefaultsToTwoSpaces) {
  ReaderPrefs p;
  EXPECT_EQ(2, p.paragraphIndentSpaces);
  EXPECT_EQ(reader_defaults::PARAGRAPH_INDENT_SPACES, p.paragraphIndentSpaces);
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
  for (unsigned value = 4; value <= UINT8_MAX; ++value) {
    EXPECT_EQ(value, foldLegacyReaderFontSize(static_cast<uint8_t>(value)));
  }
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
  legacy.paragraphIndentSpaces = 0;
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
  EXPECT_EQ(reader_defaults::PARAGRAPH_INDENT_SPACES, loaded.paragraphIndentSpaces);
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
  current.paragraphIndentSpaces = 0;
  current.paragraphNumbering = 0;
  std::stringstream ss;
  const uint8_t ver = ReaderPrefs::VERSION;
  ss.write(reinterpret_cast<const char*>(&ver), 1);
  ss.write(reinterpret_cast<const char*>(&current), sizeof(ReaderPrefs));

  ReaderPrefs loaded;
  bool migrated = true;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_FALSE(migrated);
  EXPECT_EQ(0, loaded.paragraphIndentSpaces);
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
  EXPECT_EQ(1, p.paragraphNumberSize);  // PARA_NUM_SIZE_MEDIUM
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

// Embedded Style split into a text switch and a layout switch in v12. A v11 sidecar
// stops before the layout switch, so it must be read at its own length: reading
// sizeof() instead would run off the end of the record and drop every per-book
// setting the user ever chose the first time this firmware opens the book.
TEST(ReaderPrefs, Version11SidecarKeepsEveryFieldItActuallyHeld) {
  ReaderPrefs legacy = makeSample();
  legacy.embeddedStyle = 1;  // the single v11 "Embedded Style" choice
  legacy.sbOffBar = 3;       // the last field a v11 record really carried
  std::stringstream ss;
  const uint8_t v11 = 11;
  ss.write(reinterpret_cast<const char*>(&v11), 1);
  ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V11_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(legacy.fontPointSize, loaded.fontPointSize);
  EXPECT_EQ(legacy.screenMargin, loaded.screenMargin);
  EXPECT_EQ(legacy.screenMarginTop, loaded.screenMarginTop);
  EXPECT_EQ(legacy.paragraphNumberSize, loaded.paragraphNumberSize);
  EXPECT_EQ(legacy.statusBarEnabled, loaded.statusBarEnabled);
  EXPECT_EQ(legacy.sbOffBar, loaded.sbOffBar);
  EXPECT_STREQ(legacy.sdFontFamilyName, loaded.sdFontFamilyName);
}

// The split's rule: a reader who had Embedded Style off wanted the book's own styling
// gone, so both switches start off rather than quietly handing back the book's
// margins and indents.
TEST(ReaderPrefs, Version11SidecarSeedsTheLayoutSwitchFromTheOldSingleChoice) {
  for (const uint8_t choice : {uint8_t{0}, uint8_t{1}}) {
    ReaderPrefs legacy = makeSample();
    legacy.embeddedStyle = choice;
    std::stringstream ss;
    const uint8_t v11 = 11;
    ss.write(reinterpret_cast<const char*>(&v11), 1);
    ss.write(reinterpret_cast<const char*>(&legacy), READER_PREFS_V11_SIZE);

    ReaderPrefs loaded;
    ASSERT_TRUE(readReaderPrefs(ss, loaded));
    EXPECT_EQ(choice, loaded.embeddedStyle);
    EXPECT_EQ(0, loaded.retiredEmbeddedLayoutStyle);  // folded into the one switch, then zeroed
  }
}

// The layout switch is the newest field, so it must sit last: every field above it
// keeps the offset a v11 record wrote it at.
TEST(ReaderPrefs, EachVersionStopsBeforeTheFieldTheNextOneAppended) {
  EXPECT_EQ(offsetof(ReaderPrefs, retiredEmbeddedLayoutStyle), READER_PREFS_V11_SIZE);
  EXPECT_EQ(offsetof(ReaderPrefs, sbParaPagesPos), READER_PREFS_V12_SIZE);
  EXPECT_EQ(READER_PREFS_V11_SIZE + 1, READER_PREFS_V12_SIZE);
  EXPECT_EQ(READER_PREFS_V12_SIZE + 1, READER_PREFS_V13_SIZE);
  EXPECT_EQ(READER_PREFS_V13_SIZE + 1, READER_PREFS_V14_SIZE);
  EXPECT_EQ(READER_PREFS_V14_SIZE + 1, READER_PREFS_V15_SIZE);
  EXPECT_EQ(READER_PREFS_V15_SIZE + 1, READER_PREFS_V16_SIZE);
  EXPECT_EQ(READER_PREFS_V16_SIZE + 1, READER_PREFS_V17_SIZE);
  EXPECT_EQ(READER_PREFS_V17_SIZE + 2, READER_PREFS_V18_SIZE);
  EXPECT_EQ(READER_PREFS_V18_SIZE + 1, READER_PREFS_V19_SIZE);
  EXPECT_EQ(READER_PREFS_V19_SIZE + 1, READER_PREFS_V20_SIZE);
  EXPECT_EQ(READER_PREFS_V20_SIZE + 1, READER_PREFS_V21_SIZE);
  EXPECT_EQ(READER_PREFS_V21_SIZE + 1, READER_PREFS_V22_SIZE);
  EXPECT_EQ(READER_PREFS_V22_SIZE + 1, READER_PREFS_V23_SIZE);
  EXPECT_EQ(READER_PREFS_V23_SIZE + 1, READER_PREFS_V24_SIZE);
  EXPECT_EQ(READER_PREFS_V24_SIZE + 1, READER_PREFS_V25_SIZE);
  EXPECT_EQ(READER_PREFS_V25_SIZE + 1, sizeof(ReaderPrefs));
  EXPECT_EQ(READER_PREFS_V25_SIZE, readerPrefsRecordSize(25));
  EXPECT_EQ(READER_PREFS_V24_SIZE, readerPrefsRecordSize(24));
  EXPECT_EQ(READER_PREFS_V23_SIZE, readerPrefsRecordSize(23));
  EXPECT_EQ(READER_PREFS_V22_SIZE, readerPrefsRecordSize(22));
  EXPECT_EQ(READER_PREFS_V21_SIZE, readerPrefsRecordSize(21));
  EXPECT_EQ(READER_PREFS_V20_SIZE, readerPrefsRecordSize(20));
  EXPECT_EQ(READER_PREFS_V19_SIZE, readerPrefsRecordSize(19));
  EXPECT_EQ(READER_PREFS_V18_SIZE, readerPrefsRecordSize(18));
  EXPECT_EQ(READER_PREFS_V17_SIZE, readerPrefsRecordSize(17));
  EXPECT_EQ(READER_PREFS_V16_SIZE, readerPrefsRecordSize(16));
  EXPECT_EQ(READER_PREFS_V15_SIZE, readerPrefsRecordSize(15));
  EXPECT_EQ(READER_PREFS_V13_SIZE, readerPrefsRecordSize(13));
  EXPECT_EQ(READER_PREFS_V14_SIZE, readerPrefsRecordSize(14));
  EXPECT_LT(READER_PREFS_V10_SIZE, READER_PREFS_V11_SIZE);
  EXPECT_EQ(READER_PREFS_V11_SIZE, readerPrefsRecordSize(11));
  EXPECT_EQ(READER_PREFS_V12_SIZE, readerPrefsRecordSize(12));
  EXPECT_EQ(sizeof(ReaderPrefs), readerPrefsRecordSize(ReaderPrefs::VERSION));
  EXPECT_EQ(27, ReaderPrefs::VERSION);
  EXPECT_EQ(sizeof(ReaderPrefs), readerPrefsRecordSize(26));  // v27 moved meanings, not bytes
}

// A v23 record stops before Character Spacing: it reads back as the font as designed,
// and the spec carries the signed whole pixels.
TEST(ReaderPrefs, CharacterSpacingDefaultsToNormalAndMapsSigned) {
  ReaderPrefs prefs;
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING, prefs.characterSpacing);
  EXPECT_EQ(0, makeRenderSpec(prefs, 0, 480, 800).characterSpacing);
  prefs.characterSpacing = 0;
  EXPECT_EQ(-2, makeRenderSpec(prefs, 0, 480, 800).characterSpacing);
  prefs.characterSpacing = 4;
  EXPECT_EQ(2, makeRenderSpec(prefs, 0, 480, 800).characterSpacing);
  prefs.characterSpacing = 200;  // a corrupt byte clamps rather than wrapping
  EXPECT_EQ(2, makeRenderSpec(prefs, 0, 480, 800).characterSpacing);
}

TEST(ReaderPrefs, AV12RecordKeepsItsFieldsAndLeavesTheNewItemOff) {
  // The byte appended by v13 is not in a v12 record. Everything written before it
  // has to read back unchanged, and the new item has to land on its default.
  ReaderPrefs written;
  written.retiredEmbeddedLayoutStyle = 0;
  written.sbSessionPagesPos = 3;
  written.sbParaPagesPos = 6;

  const auto* raw = reinterpret_cast<const uint8_t*>(&written);
  const std::string record(reinterpret_cast<const char*>(raw), READER_PREFS_V12_SIZE);

  ReaderPrefs read;
  std::memcpy(&read, record.data(), record.size());
  EXPECT_EQ(read.retiredEmbeddedLayoutStyle, 0);
  EXPECT_EQ(read.sbSessionPagesPos, 3);
  EXPECT_EQ(read.sbParaPagesPos, 0) << "a v12 record cannot carry this field";
}

// ── The interim v11 layout ────────────────────────────────────────────────────
// Between the Embedded Style split and the version bump that should have come with
// it, main wrote records STAMPED 11 that carry embeddedLayoutStyle in the middle of
// the struct, one byte longer than a real v11 record. Reading one at the stable v11
// length shifts every field after embeddedTextStyle by one — the font name loses its
// first character and the whole status bar block slides — and the read still reports
// success. The two formats differ only in length, so length is what tells them apart.
std::string interimV11Record(const ReaderPrefs& p) {
  const auto* raw = reinterpret_cast<const uint8_t*>(&p);
  constexpr size_t split = offsetof(ReaderPrefs, retiredTextAntiAliasing);
  std::string out;
  out.append(reinterpret_cast<const char*>(raw), split);
  out.push_back(static_cast<char>(p.retiredEmbeddedLayoutStyle));  // sat here in the interim build
  out.append(reinterpret_cast<const char*>(raw + split), READER_PREFS_V11_SIZE - split);
  return out;  // READER_PREFS_V11_SIZE + 1 bytes
}

TEST(ReaderPrefs, TheInterimV11LayoutIsUnshiftedRatherThanMisread) {
  ReaderPrefs written = makeSample();
  written.embeddedStyle = 1;
  written.retiredEmbeddedLayoutStyle = 0;  // this interim record really did carry its own layout switch
  written.retiredTextAntiAliasing = 1;
  written.imageRendering = 2;
  std::strncpy(written.sdFontFamilyName, "Bookerly", sizeof(written.sdFontFamilyName) - 1);
  written.sbOffBar = 3;

  const std::string record = interimV11Record(written);
  ASSERT_EQ(READER_PREFS_V11_SIZE + 1, record.size());

  std::stringstream ss;
  const uint8_t v11 = 11;
  ss.write(reinterpret_cast<const char*>(&v11), 1);
  ss.write(record.data(), static_cast<std::streamsize>(record.size()));

  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(ss, loaded));
  EXPECT_EQ(0, loaded.embeddedStyle);            // text on, layout off: either off means off
  EXPECT_EQ(0, loaded.retiredTextAntiAliasing);  // retired: read, then zeroed
  EXPECT_EQ(0, loaded.imageRendering);           // Placeholder was inert before v27, so it resets to Display
  EXPECT_STREQ("Bookerly", loaded.sdFontFamilyName);
  EXPECT_EQ(3, loaded.sbOffBar);
  EXPECT_EQ(written.statusBarEnabled, loaded.statusBarEnabled);
  EXPECT_EQ(written.paragraphNumberSize, loaded.paragraphNumberSize);
}

// A stable v11 record is the shorter of the two and must still read as itself.
TEST(ReaderPrefs, AStableV11RecordIsNotMistakenForTheInterimOne) {
  ReaderPrefs written = makeSample();
  written.embeddedStyle = 0;
  written.retiredTextAntiAliasing = 1;
  written.sbOffBar = 3;

  std::stringstream ss;
  const uint8_t v11 = 11;
  ss.write(reinterpret_cast<const char*>(&v11), 1);
  ss.write(reinterpret_cast<const char*>(&written), READER_PREFS_V11_SIZE);

  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(ss, loaded));
  EXPECT_EQ(0, loaded.embeddedStyle);            // the layout half was seeded from the single old choice
  EXPECT_EQ(0, loaded.retiredTextAntiAliasing);  // retired: read, then zeroed
  EXPECT_EQ(3, loaded.sbOffBar);
}

// The status bar block is only missing from a record older than v11, and the on/off
// byte only from one older than v10. Seeding either on any migrated record would let a
// version bump silently replace a per-book bar the user configured.
TEST(ReaderPrefs, ReadReportsTheVersionTheRecordWasWrittenAt) {
  ReaderPrefs written = makeSample();
  std::stringstream ss;
  const uint8_t v11 = 11;
  ss.write(reinterpret_cast<const char*>(&v11), 1);
  ss.write(reinterpret_cast<const char*>(&written), READER_PREFS_V11_SIZE);

  ReaderPrefs loaded;
  bool migrated = false;
  uint8_t fromVersion = 0;
  ASSERT_TRUE(readReaderPrefs(ss, loaded, &migrated, &fromVersion));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(11, fromVersion);

  std::stringstream current;
  writeReaderPrefs(current, written);
  ReaderPrefs back;
  ASSERT_TRUE(readReaderPrefs(current, back, &migrated, &fromVersion));
  EXPECT_FALSE(migrated);
  EXPECT_EQ(ReaderPrefs::VERSION, fromVersion);
}

TEST(ReaderPrefs, OldSidecarsKeepTheirLookAndDefaultWordSpacingToHundred) {
  for (uint8_t version = 9; version <= 13; ++version) {
    auto old = makeSample();
    old.wordSpacing = 150;  // outside the old record, must not be read
    std::stringstream file;
    file.put(static_cast<char>(version));
    file.write(reinterpret_cast<const char*>(&old), readerPrefsRecordSize(version));
    ReaderPrefs loaded;
    bool migrated = false;
    ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
    EXPECT_TRUE(migrated);
    EXPECT_EQ(100, loaded.wordSpacing);
    EXPECT_EQ(old.fontPointSize, loaded.fontPointSize);
    EXPECT_EQ(old.screenMargin, loaded.screenMargin);
  }
}

TEST(ReaderPrefs, WordSpacingRoundTripsAtEverySetting) {
  for (uint8_t percent = 50; percent <= 200; percent += 25) {
    auto original = makeSample();
    original.wordSpacing = percent;
    std::stringstream file;
    writeReaderPrefs(file, original);
    ReaderPrefs loaded;
    ASSERT_TRUE(readReaderPrefs(file, loaded));
    expectEqual(original, loaded);
  }
}

TEST(ReaderPrefs, RetiredByteKeepsLegacyOffsetsAndDoesNotAffectComparisons) {
  // Frozen v14 layout: the retired toggle sat at byte 14, text style at 15,
  // font name at 23, and word spacing at 76. Do not derive these from offsetof.
  std::string record(78, '\0');
  record[0] = 14;
  record[1 + 14] = 1;
  record[1 + 15] = 1;
  record[1 + READER_PREFS_V11_SIZE] = 1;  // the v12 layout switch, on
  record.replace(1 + 23, 8, "Bookerly");
  record[1 + 76] = 125;
  std::stringstream in(record);
  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(in, loaded));
  EXPECT_EQ(0, loaded.reserved14);
  EXPECT_EQ(1, loaded.embeddedStyle);
  EXPECT_STREQ("Bookerly", loaded.sdFontFamilyName);
  EXPECT_EQ(125, loaded.wordSpacing);
  record[1 + 14] = 0;
  std::stringstream disabled(record);
  ReaderPrefs other;
  ASSERT_TRUE(readReaderPrefs(disabled, other));
  EXPECT_EQ(0, std::memcmp(&loaded, &other, sizeof(loaded)));
}

// The EPUB loader decides whether to parse a book's stylesheet from the book's own
// look, not the global one: a book with Embedded Style on needs its CSS even when the
// global switch is off.
TEST(ReaderPrefs, WantsBookCssWhenTheEmbeddedSwitchIsOn) {
  ReaderPrefs p;
  p.embeddedStyle = 0;
  EXPECT_FALSE(wantsBookCss(p));
  p.embeddedStyle = 1;
  EXPECT_TRUE(wantsBookCss(p));
}

TEST(ReaderPrefs, SettleSidecarFoldsRetiredWholeBookNumbering) {
  ReaderPrefs p = makeSample();
  p.paragraphNumbering = 2;  // whole-book numbering, removed
  settleSidecarPrefs(p, ReaderPrefs::VERSION, ReaderPrefs{});
  EXPECT_EQ(reader_defaults::PARAGRAPH_NUMBERING, p.paragraphNumbering);
}

TEST(ReaderPrefs, SettleSidecarSeedsWholeStatusBarForRecordsWithoutOne) {
  ReaderPrefs global;
  global.sbBatteryPos = 3;
  global.sbOffBar = 2;
  ReaderPrefs old;
  old.sbBatteryPos = 1;
  settleSidecarPrefs(old, FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR - 1, global);
  EXPECT_EQ(3, old.sbBatteryPos);
  EXPECT_EQ(2, old.sbOffBar);
}

// The bar layout is the reader's, not the book's: a book with its own look still shows
// the global layout, so Settings > Customise Status Bar reaches it. Only the show/hide
// switch and the hidden-bar progress bar stay the book's own.
TEST(ReaderPrefs, SettleSidecarTakesGlobalBarLayoutKeepingBookSwitches) {
  ReaderPrefs global;
  global.sbBatteryPos = 4;
  global.sbTitlePos = 5;
  global.sbBookTitlePos = 1;
  global.sbBookPctPos = 3;
  global.statusBarEnabled = 1;
  global.sbOffBar = 0;
  ReaderPrefs book;
  book.sbBatteryPos = 0;
  book.sbTitlePos = 2;
  book.sbBookTitlePos = 0;
  book.sbBookPctPos = 6;
  book.statusBarEnabled = 0;
  book.sbOffBar = 2;
  settleSidecarPrefs(book, FIRST_VERSION_WITH_PER_BOOK_STATUS_BAR, global);
  EXPECT_EQ(4, book.sbBatteryPos);
  EXPECT_EQ(5, book.sbTitlePos);
  EXPECT_EQ(1, book.sbBookTitlePos);
  EXPECT_EQ(3, book.sbBookPctPos);
  EXPECT_EQ(0, book.statusBarEnabled);
  EXPECT_EQ(2, book.sbOffBar);
}

// Every status bar field survives a trip from a book's look to a block and back, so a
// field added to the list but missed by a copier fails here.
TEST(StatusBarBlock, RoundTripsThroughABooksLook) {
  StatusBarBlock b;
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&b);
  for (size_t i = 0; i < sizeof(b); ++i) bytes[i] = static_cast<uint8_t>(i + 1);
  ReaderPrefs p;
  setStatusBarOf(p, b);
  const StatusBarBlock back = statusBarOf(p);
  EXPECT_EQ(0, std::memcmp(&b, &back, sizeof(b)));
}

TEST(StatusBarBlock, HiddenBarKeepsItsProgressBarsOnlyWhenAsked) {
  StatusBarBlock b;
  b.enabled = 0;
  b.barThickness = 2;
  b.offBar = StatusBarBlock::OFF_BAR_OFF;
  EXPECT_FALSE(b.progressBarsVisible());
  b.offBar = 1;  // Slim
  EXPECT_TRUE(b.progressBarsVisible());
  EXPECT_EQ(0, b.activeBarThickness());  // the hidden-bar size, not the bar's own
  b.enabled = 1;
  EXPECT_EQ(2, b.activeBarThickness());
}

TEST(ReaderPrefs, RetiredTimeLeftBytesAreZeroedOnRead) {
  // A record written while Time Left in Chapter / Book existed still carries the anchors.
  // The layout keeps their bytes, but a read drops the values so a book that had one set
  // is not seen as custom by the whole-blob compare against global.
  auto old = makeSample();
  old.retiredChapterTime = 3;
  old.retiredBookTime = 4;
  for (const uint8_t version : {static_cast<uint8_t>(18), ReaderPrefs::VERSION}) {
    std::stringstream file;
    file.put(static_cast<char>(version));
    file.write(reinterpret_cast<const char*>(&old), readerPrefsRecordSize(version));
    ReaderPrefs loaded;
    ASSERT_TRUE(readReaderPrefs(file, loaded));
    EXPECT_EQ(0, loaded.retiredChapterTime);
    EXPECT_EQ(0, loaded.retiredBookTime);
  }
}

TEST(ReaderPrefs, AV18RecordKeepsNormalContrast) {
  auto old = makeSample();
  old.retiredTextContrast = 2;  // outside the v18 record, must not be read
  std::stringstream file;
  file.put(static_cast<char>(18));
  file.write(reinterpret_cast<const char*>(&old), readerPrefsRecordSize(18));
  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(0, loaded.retiredTextContrast);
}

TEST(ReaderPrefs, AV25RecordPutsNumbersByTheText) {
  auto old = makeSample();
  old.paragraphNumberPlace = 0;  // outside the v25 record, must not be read
  std::stringstream file;
  file.put(static_cast<char>(25));
  file.write(reinterpret_cast<const char*>(&old), readerPrefsRecordSize(25));
  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(1, loaded.paragraphNumberPlace);
}

TEST(ReaderPrefs, AV22RecordLeavesTheBookTitleItemOff) {
  auto old = makeSample();
  old.sbBookTitlePos = 1;  // outside the v22 record, must not be read
  std::stringstream file;
  file.put(static_cast<char>(22));
  file.write(reinterpret_cast<const char*>(&old), readerPrefsRecordSize(22));
  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(0, loaded.sbBookTitlePos);
}

TEST(ReaderPrefs, ABookTitleSourceFoldsOntoTheBookTitleItem) {
  ReaderPrefs p;
  p.sbTitleSource = 0;  // Book, from before Title Source was retired
  p.sbTitlePos = 2;     // TC
  p.sbBookTitlePos = 0;
  const StatusBarBlock b = statusBarOf(p);
  EXPECT_EQ(0, b.titlePos);
  EXPECT_EQ(2, b.bookTitlePos);
  EXPECT_EQ(1, b.titleSource);

  p.sbBookTitlePos = 4;  // already placed: it keeps its own anchor
  EXPECT_EQ(4, statusBarOf(p).bookTitlePos);
  EXPECT_EQ(0, statusBarOf(p).titlePos);
}

TEST(ReaderPrefs, TitleSlotStacksTheBookAboveTheChapterOnOneAnchor) {
  StatusBarBlock b;
  b.titleSource = 1;
  b.titlePos = 1;      // TL
  b.bookTitlePos = 1;  // TL
  auto s = b.titleSlot(true);
  EXPECT_EQ(1, s.anchor);
  EXPECT_TRUE(s.chapter);
  EXPECT_TRUE(s.bookAbove);
  EXPECT_EQ(0, s.bookItem);

  b.bookTitlePos = 2;  // TC: same edge, so it stacks at the chapter's anchor
  s = b.titleSlot(true);
  EXPECT_EQ(1, s.anchor);
  EXPECT_TRUE(s.bookAbove);
  EXPECT_EQ(0, s.bookItem);

  b.bookTitlePos = 6;  // BR: other edge, its own one-line item
  s = b.titleSlot(true);
  EXPECT_FALSE(s.bookAbove);
  EXPECT_EQ(6, s.bookItem);

  s = b.titleSlot(false);  // chapterless: the book title takes the slot at its anchor
  EXPECT_EQ(6, s.anchor);
  EXPECT_FALSE(s.chapter);
  EXPECT_EQ(0, s.bookItem);

  b.bookTitlePos = 0;  // chapterless, no Book Title: the chapter anchor shows the book
  EXPECT_EQ(1, b.titleSlot(false).anchor);
}

// ── v27: the lean typography record ───────────────────────────────────────────
// v27 keeps v26's layout, so a v26 record is read at full length and converted: the indent
// byte (Book or percent, in two bytes) becomes a space count, Character Spacing becomes
// whole pixels, the two style switches become one, and the retired switches are zeroed.
namespace {
std::stringstream asRecord(const uint8_t version, const ReaderPrefs& p) {
  std::stringstream file;
  file.put(static_cast<char>(version));
  file.write(reinterpret_cast<const char*>(&p), readerPrefsRecordSize(version));
  return file;
}

ReaderPrefs legacyLook() {
  ReaderPrefs old;
  old.retiredFirstLineIndentMode = 1;
  old.paragraphIndentSpaces = 23;  // the old percent byte: the old default
  old.retiredEmbeddedLayoutStyle = 1;
  old.embeddedStyle = 1;
  old.characterSpacing = 7;  // +1 px, in half pixels
  old.imageRendering = 2;    // Suppress: inert since 2026-08-11
  old.wordSpacing = 107;
  old.lineSpacingPercent = 40;
  old.retiredKerning = old.retiredLigatures = old.retiredLinkUnderline = 0;
  old.retiredBookMargins = 0;
  old.retiredHeadingPageBreak = 1;
  old.retiredWordExpansion = 2;
  old.retiredFillPages = 1;
  old.retiredParagraphSpacing = 50;
  return old;
}
}  // namespace

TEST(ReaderPrefs, AV26RecordIsConvertedToTheLeanMeanings) {
  std::stringstream file = asRecord(26, legacyLook());
  ReaderPrefs loaded;
  bool migrated = false;
  ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
  EXPECT_TRUE(migrated);
  EXPECT_EQ(2, loaded.paragraphIndentSpaces);  // 23 percent of the column, now two spaces
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING + 1, loaded.characterSpacing);
  EXPECT_EQ(1, loaded.embeddedStyle);
  EXPECT_EQ(0, loaded.imageRendering);
  EXPECT_EQ(100, loaded.wordSpacing);  // 107 lands on the 25 percent grid
  EXPECT_EQ(reader_defaults::MIN_LINE_SPACING_PERCENT, loaded.lineSpacingPercent);
  // Every retired byte reads back zero, so the whole-blob comparison stays exact.
  ReaderPrefs expected = loaded;
  expected.retiredParagraphSpacing = expected.retiredFirstLineIndentMode = expected.retiredEmbeddedLayoutStyle = 0;
  expected.retiredKerning = expected.retiredLigatures = expected.retiredLinkUnderline = 0;
  expected.retiredBookMargins = expected.retiredHeadingPageBreak = expected.retiredWordExpansion = 0;
  expected.retiredFillPages = 0;
  EXPECT_EQ(0, std::memcmp(&expected, &loaded, sizeof(ReaderPrefs)));
}

TEST(ReaderPrefs, AV26RecordWithEitherStyleHalfOffComesBackWithTheStyleOff) {
  for (const uint8_t layout : {uint8_t{0}, uint8_t{1}}) {
    for (const uint8_t text : {uint8_t{0}, uint8_t{1}}) {
      ReaderPrefs old = legacyLook();
      old.embeddedStyle = text;
      old.retiredEmbeddedLayoutStyle = layout;
      std::stringstream file = asRecord(26, old);
      ReaderPrefs loaded;
      ASSERT_TRUE(readReaderPrefs(file, loaded));
      EXPECT_EQ(text && layout ? 1 : 0, loaded.embeddedStyle) << int(text) << int(layout);
    }
  }
}

TEST(ReaderPrefs, TheOldIndentModesMapToSpaces) {
  const auto indentOf = [](uint8_t mode, uint8_t percent) {
    ReaderPrefs old = legacyLook();
    old.retiredFirstLineIndentMode = mode;
    old.paragraphIndentSpaces = percent;
    std::stringstream file = asRecord(26, old);
    ReaderPrefs loaded;
    EXPECT_TRUE(readReaderPrefs(file, loaded));
    return loaded.paragraphIndentSpaces;
  };
  EXPECT_EQ(2, indentOf(0, 0));  // Book: no publisher choice left, the default
  EXPECT_EQ(0, indentOf(1, 0));
  EXPECT_EQ(1, indentOf(1, 5));
  EXPECT_EQ(2, indentOf(1, 20));
  EXPECT_EQ(5, indentOf(1, 100));
}

// A v23 record has no Character Spacing byte, so what sits there is the constructed
// default and must not be run through the half-pixel conversion.
TEST(ReaderPrefs, AV23RecordKeepsTheFontAsDesignedAndAV24RecordConverts) {
  ReaderPrefs old = legacyLook();
  old.characterSpacing = 9;  // outside a v23 record, must not be read or converted
  std::stringstream v23 = asRecord(23, old);
  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(v23, loaded));
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING, loaded.characterSpacing);

  old.characterSpacing = 3;  // -1 px, in half pixels: -2 half pixels
  std::stringstream v24 = asRecord(24, old);
  ASSERT_TRUE(readReaderPrefs(v24, loaded));
  EXPECT_EQ(reader_defaults::CHARACTER_SPACING - 1, loaded.characterSpacing);
}

TEST(ReaderPrefs, AV26RecordSideMarginsLandOnTheNewRangeAndAllSidesCarriesThem) {
  ReaderPrefs old = legacyLook();
  old.screenMargin = 60;
  old.screenMarginTop = 60;
  old.screenMarginBottom = 60;
  old.marginLinkMode = 2;  // All Sides
  std::stringstream file = asRecord(26, old);
  ReaderPrefs loaded;
  ASSERT_TRUE(readReaderPrefs(file, loaded));
  EXPECT_EQ(40, loaded.screenMargin);
  EXPECT_EQ(40, loaded.screenMarginTop);
  EXPECT_EQ(40, loaded.screenMarginBottom);

  old.screenMargin = 17;   // an old default lands on the grid
  old.marginLinkMode = 0;  // Separate keeps the vertical margins as they were
  old.screenMarginTop = 60;
  std::stringstream separate = asRecord(26, old);
  ASSERT_TRUE(readReaderPrefs(separate, loaded));
  EXPECT_EQ(15, loaded.screenMargin);
  EXPECT_EQ(60, loaded.screenMarginTop);
}

TEST(ReaderPrefs, ACurrentRecordIsNeverConverted) {
  ReaderPrefs now = makeSample();
  now.imageRendering = 2;  // Suppress is a real choice again
  now.characterSpacing = 4;
  now.wordSpacing = 175;
  std::stringstream file;
  writeReaderPrefs(file, now);
  ReaderPrefs loaded;
  bool migrated = true;
  ASSERT_TRUE(readReaderPrefs(file, loaded, &migrated));
  EXPECT_FALSE(migrated);
  expectEqual(now, loaded);
}
