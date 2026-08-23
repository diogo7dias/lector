#pragma once

// The reader-look field list, declared once.
//
// The same set of per-book "look" fields is copied between CrossPointSettings and
// ReaderPrefs by six different pieces of code (fromGlobal, applyReaderPrefs, the three
// status bar block copiers, and the in-book status bar editor). Each used to spell the
// list out by hand, so adding a field meant remembering every site: a missed line does
// not fail to compile, it silently drops the setting for every book on the card.
//
// These macros are that list. Every copier expands one of them, and the counts below
// are checked against sizeof(ReaderPrefs) in ReaderPrefs.h, so a field added to the
// struct and to no list breaks the build instead of shipping.
//
// Macros rather than a table of member pointers because the two structs are declared in
// headers that cannot see each other: CrossPointSettings.h pulls in the Arduino stack,
// while ReaderPrefs.h stays host-buildable for the tests. A macro list needs neither
// type to be complete, so both sides expand the same names.
//
// This header deliberately includes nothing and declares nothing.

// Rows of the in-book Reader Settings screen. These are the fields the edit overlay
// round-trips through the global singleton, so they travel in both directions.
// X(name) — the member name is identical on ReaderPrefs and CrossPointSettings.
#define READER_LOOK_SCREEN_FIELDS(X) \
  X(fontFamily)                      \
  X(fontPointSize)                   \
  X(lineSpacingPercent)              \
  X(paragraphAlignment)              \
  X(extraParagraphSpacing)           \
  X(paragraphSpacing)                \
  X(screenMargin)                    \
  X(screenMarginTop)                 \
  X(screenMarginBottom)              \
  X(marginLinkMode)                  \
  X(dynamicMargins)                  \
  X(firstLineIndentMode)             \
  X(firstLineIndentPercent)          \
  X(focusReadingEnabled)             \
  X(guideDotsEnabled)                \
  X(guideDotsHidden)                 \
  X(hyphenationEnabled)              \
  X(embeddedTextStyle)               \
  X(embeddedLayoutStyle)             \
  X(textAntiAliasing)                \
  X(imageRendering)

// Per-book toggles that live in the in-book menu, not on the Reader Settings screen.
// The edit overlay does not carry them, so after an edit the book's own values must be
// restored over the overlaid globals — see restoreBookOnlyFields() in ReaderPrefs.h.
// X(name) — identical on both structs. statusBarEnabled is book-only too, but it rides
// with the status bar block below, which is where its global counterpart is named.
#define READER_LOOK_BOOK_FIELDS(X) \
  X(paragraphNumbering)            \
  X(paragraphNumberSize)           \
  X(paperbackLookBody)             \
  X(paperbackLookStatus)

// The status bar layout, moved as one block between CrossPointSettings, StatusBarBlock
// and ReaderPrefs. X(prefsName, settingsName, blockName) — the three structs disagree
// on names, so all three are spelled out.
#define READER_STATUS_BAR_FIELDS(X)                        \
  X(statusBarEnabled, sbEnabled, enabled)                  \
  X(sbBatteryPos, sbBatteryPos, batteryPos)                \
  X(sbClockPos, sbClockPos, clockPos)                      \
  X(sbTitlePos, sbTitlePos, titlePos)                      \
  X(sbTitleSource, sbTitleSource, titleSource)             \
  X(sbTitleTruncate, sbTitleTruncate, titleTruncate)       \
  X(sbPagePos, sbPagePos, pagePos)                         \
  X(sbPageFormat, sbPageFormat, pageFormat)                \
  X(sbBookPctPos, sbBookPctPos, bookPctPos)                \
  X(sbChapterPctPos, sbChapterPctPos, chapterPctPos)       \
  X(sbChapterNumPos, sbChapterNumPos, chapterNumPos)       \
  X(sbSessionPagesPos, sbSessionPagesPos, sessionPagesPos) \
  X(sbBookBar, sbBookBar, bookBar)                         \
  X(sbChapterBar, sbChapterBar, chapterBar)                \
  X(sbBarThickness, sbBarThickness, barThickness)          \
  X(sbFloatingBar, sbFloatingBar, floatingBar)             \
  X(sbBarOutline, sbBarOutline, barOutline)                \
  X(sbOffBar, sbOffBar, offBar)
