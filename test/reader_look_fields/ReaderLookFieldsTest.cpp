// Host tests for the reader-look field lists in ReaderLookFields.h.
//
// The lists are what every copier between CrossPointSettings and ReaderPrefs expands, so
// what matters is that they stay complete and disjoint. The coverage guard itself is a
// static_assert in ReaderPrefs.h; these tests pin the behaviour that guard protects, and
// they derive their expectations from the lists rather than restating the field names —
// a field added to a list is picked up here with no edit to this file.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <set>
#include <string>

#include "ReaderPrefs.h"

namespace {

// Every uint8_t field, set to a value nothing else uses, so a copier that reads the
// wrong member is caught by the value and not just by the byte count.
ReaderPrefs makeAllDistinct() {
  ReaderPrefs p;
  uint8_t next = 1;
#define CP_SET_FIELD(name) p.name = next++;
  READER_LOOK_SCREEN_FIELDS(CP_SET_FIELD)
  READER_LOOK_BOOK_FIELDS(CP_SET_FIELD)
#undef CP_SET_FIELD
#define CP_SET_SB_FIELD(prefsName, settingsName, blockName) p.prefsName = next++;
  READER_STATUS_BAR_FIELDS(CP_SET_SB_FIELD)
#undef CP_SET_SB_FIELD
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, "Literata", sizeof(p.sdFontFamilyName) - 1);
  return p;
}

std::set<std::string> screenFieldNames() {
  std::set<std::string> names;
#define CP_NAME(name) names.insert(#name);
  READER_LOOK_SCREEN_FIELDS(CP_NAME)
#undef CP_NAME
  return names;
}

std::set<std::string> bookFieldNames() {
  std::set<std::string> names;
#define CP_NAME(name) names.insert(#name);
  READER_LOOK_BOOK_FIELDS(CP_NAME)
#undef CP_NAME
  return names;
}

std::set<std::string> statusBarFieldNames() {
  std::set<std::string> names;
#define CP_SB_NAME(prefsName, settingsName, blockName) names.insert(#prefsName);
  READER_STATUS_BAR_FIELDS(CP_SB_NAME)
#undef CP_SB_NAME
  return names;
}

TEST(ReaderLookFields, EveryPrefsByteIsNamedByExactlyOneList) {
  const size_t named = screenFieldNames().size() + bookFieldNames().size() + statusBarFieldNames().size();
  // Set sizes, so a name repeated inside one list collapses and fails here.
  EXPECT_EQ(named, reader_look::SCREEN_FIELD_COUNT + reader_look::BOOK_FIELD_COUNT +
                       reader_look::STATUS_BAR_FIELD_COUNT);
  EXPECT_EQ(named + sizeof(ReaderPrefs::sdFontFamilyName), sizeof(ReaderPrefs));
}

TEST(ReaderLookFields, TheListsDoNotOverlap) {
  std::set<std::string> all;
  size_t inserted = 0;
  for (const auto& list : {screenFieldNames(), bookFieldNames(), statusBarFieldNames()}) {
    inserted += list.size();
    all.insert(list.begin(), list.end());
  }
  EXPECT_EQ(all.size(), inserted);
}

TEST(ReaderLookFields, SettingTheWholeListTouchesEveryByte) {
  const ReaderPrefs p = makeAllDistinct();
  const auto* bytes = reinterpret_cast<const uint8_t*>(&p);
  // The font name is the only run of bytes the lists do not name; every uint8_t field
  // got a distinct non-zero value, so a byte left at zero means an unnamed field.
  for (size_t i = 0; i < sizeof(ReaderPrefs); ++i) {
    if (i >= offsetof(ReaderPrefs, sdFontFamilyName) &&
        i < offsetof(ReaderPrefs, sdFontFamilyName) + sizeof(ReaderPrefs::sdFontFamilyName)) {
      continue;
    }
    EXPECT_NE(bytes[i], 0) << "byte " << i << " belongs to no field list";
  }
}

TEST(ReaderLookFields, RestoreBookOnlyFieldsTakesTheBookToggles) {
  const ReaderPrefs book = makeAllDistinct();
  ReaderPrefs target;  // constructed defaults stand in for the overlaid global values
  restoreBookOnlyFields(target, book);

  EXPECT_EQ(target.paragraphNumbering, book.paragraphNumbering);
  EXPECT_EQ(target.paragraphNumberSize, book.paragraphNumberSize);
  EXPECT_EQ(target.paperbackLookBody, book.paperbackLookBody);
  EXPECT_EQ(target.paperbackLookStatus, book.paperbackLookStatus);
  EXPECT_EQ(target.statusBarEnabled, book.statusBarEnabled);
}

TEST(ReaderLookFields, RestoreBookOnlyFieldsLeavesTheScreenRowsAlone) {
  const ReaderPrefs book = makeAllDistinct();
  const ReaderPrefs untouched;
  ReaderPrefs target;
  restoreBookOnlyFields(target, book);

#define CP_EXPECT_UNCHANGED(name) EXPECT_EQ(target.name, untouched.name) << #name " must not be restored";
  READER_LOOK_SCREEN_FIELDS(CP_EXPECT_UNCHANGED)
#undef CP_EXPECT_UNCHANGED
}

TEST(ReaderLookFields, RestoreBookOnlyFieldsLeavesTheStatusBarLayoutAlone) {
  const ReaderPrefs book = makeAllDistinct();
  const ReaderPrefs untouched;
  ReaderPrefs target;
  restoreBookOnlyFields(target, book);

  // statusBarEnabled is the one field of that list restoreBookOnlyFields does carry: the
  // bar's on/off switch is per book, its layout follows whatever the bar editor left.
#define CP_EXPECT_SB_UNCHANGED(prefsName, settingsName, blockName)     \
  if (std::string(#prefsName) != "statusBarEnabled") {                 \
    EXPECT_EQ(target.prefsName, untouched.prefsName) << #prefsName;    \
  }
  READER_STATUS_BAR_FIELDS(CP_EXPECT_SB_UNCHANGED)
#undef CP_EXPECT_SB_UNCHANGED
}

TEST(ReaderLookFields, AdoptStatusBarFromTakesTheWholeBlock) {
  const ReaderPrefs source = makeAllDistinct();
  ReaderPrefs target;
  target.adoptStatusBarFrom(source);

#define CP_EXPECT_SB_ADOPTED(prefsName, settingsName, blockName) EXPECT_EQ(target.prefsName, source.prefsName) << #prefsName;
  READER_STATUS_BAR_FIELDS(CP_EXPECT_SB_ADOPTED)
#undef CP_EXPECT_SB_ADOPTED
}

TEST(ReaderLookFields, AdoptStatusBarFromLeavesTheReaderLookAlone) {
  const ReaderPrefs source = makeAllDistinct();
  const ReaderPrefs untouched;
  ReaderPrefs target;
  target.adoptStatusBarFrom(source);

#define CP_EXPECT_LOOK_UNCHANGED(name) EXPECT_EQ(target.name, untouched.name) << #name;
  READER_LOOK_SCREEN_FIELDS(CP_EXPECT_LOOK_UNCHANGED)
  READER_LOOK_BOOK_FIELDS(CP_EXPECT_LOOK_UNCHANGED)
#undef CP_EXPECT_LOOK_UNCHANGED
}

}  // namespace
