#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ReaderPresetNames.h"

using namespace readerpreset;

// ── sanitizeName ────────────────────────────────────────────────────────────

TEST(SanitizeName, KeepsAnOrdinaryName) { EXPECT_EQ(sanitizeName("Night"), "Night"); }

TEST(SanitizeName, TrimsSurroundingBlanks) {
  EXPECT_EQ(sanitizeName("  Night  "), "Night");
  EXPECT_EQ(sanitizeName("\t\nNight \r"), "Night");
}

TEST(SanitizeName, KeepsBlanksInsideTheName) { EXPECT_EQ(sanitizeName("  Big Serif Night  "), "Big Serif Night"); }

TEST(SanitizeName, ClipsAtTheMaximumLength) {
  const std::string tooLong(MAX_NAME_LENGTH + 5, 'x');
  const std::string clipped = sanitizeName(tooLong);
  EXPECT_EQ(clipped.size(), MAX_NAME_LENGTH);
  EXPECT_EQ(clipped, std::string(MAX_NAME_LENGTH, 'x'));
}

TEST(SanitizeName, KeepsANameOfExactlyTheMaximumLength) {
  const std::string exact(MAX_NAME_LENGTH, 'y');
  EXPECT_EQ(sanitizeName(exact), exact);
}

TEST(SanitizeName, TrimsBeforeClippingSoBlanksDoNotEatTheName) {
  // The blanks must not count towards the limit, or a padded name loses real characters.
  const std::string padded = "    " + std::string(MAX_NAME_LENGTH, 'z') + "    ";
  EXPECT_EQ(sanitizeName(padded), std::string(MAX_NAME_LENGTH, 'z'));
}

TEST(SanitizeName, BlankInputReturnsEmpty) {
  EXPECT_EQ(sanitizeName(""), "");
  EXPECT_EQ(sanitizeName("    "), "");
  EXPECT_EQ(sanitizeName("\t \n"), "");
}

// ── makeUniqueName ──────────────────────────────────────────────────────────

TEST(MakeUniqueName, KeepsAFreeName) {
  const std::vector<std::string> existing = {"Sepia", "Large Print"};
  EXPECT_EQ(makeUniqueName("Night", existing), "Night");
  EXPECT_EQ(makeUniqueName("Night", {}), "Night");
}

TEST(MakeUniqueName, SanitizesTheDesiredNameFirst) { EXPECT_EQ(makeUniqueName("  Night  ", {}), "Night"); }

TEST(MakeUniqueName, BlankInputReturnsEmpty) {
  // Empty is the caller's signal to fall back to its own suggestion.
  EXPECT_EQ(makeUniqueName("   ", {"Night"}), "");
  EXPECT_EQ(makeUniqueName("", {}), "");
}

TEST(MakeUniqueName, AppendsTwoOnACollision) { EXPECT_EQ(makeUniqueName("Night", {"Night"}), "Night 2"); }

TEST(MakeUniqueName, CollidesCaseInsensitively) {
  EXPECT_EQ(makeUniqueName("Night", {"night"}), "Night 2");
  EXPECT_EQ(makeUniqueName("NIGHT", {"NiGhT"}), "NIGHT 2");
}

TEST(MakeUniqueName, WalksPastTakenSuffixes) {
  EXPECT_EQ(makeUniqueName("Night", {"Night", "Night 2"}), "Night 3");
  // The already-numbered names are matched case-insensitively too.
  EXPECT_EQ(makeUniqueName("night", {"NIGHT", "night 2", "Night 3"}), "night 4");
}

TEST(MakeUniqueName, TrimsTheBaseToMakeRoomForTheSuffix) {
  // A 20-character base plus " 2" would be 22, so the BASE loses two characters and the
  // suffix survives whole — a clipped suffix would collide with the name it separates from.
  const std::string base = "Midnight Reading Ink";
  ASSERT_EQ(base.size(), MAX_NAME_LENGTH);
  const std::string unique = makeUniqueName(base, {base});
  EXPECT_EQ(unique, "Midnight Reading I 2");
  EXPECT_EQ(unique.size(), MAX_NAME_LENGTH);
  EXPECT_EQ(unique.substr(unique.size() - 2), " 2");
}

TEST(MakeUniqueName, TrimsForATwoDigitSuffixToo) {
  const std::string base(MAX_NAME_LENGTH, 'a');
  std::vector<std::string> existing = {base};
  for (int n = 2; n <= 10; n++) {
    existing.push_back(makeUniqueName(base, existing));
  }
  const std::string unique = makeUniqueName(base, existing);
  // " 11" is three characters, so the base keeps 17 of its 20.
  EXPECT_EQ(unique, std::string(MAX_NAME_LENGTH - 3, 'a') + " 11");
  EXPECT_EQ(unique.size(), MAX_NAME_LENGTH);
}

TEST(MakeUniqueName, SkipIndexLetsARenameKeepItsOwnName) {
  const std::vector<std::string> existing = {"Night", "Sepia"};
  EXPECT_EQ(makeUniqueName("Night", existing, 0), "Night");
  // Same name, different case: still its own entry, still allowed.
  EXPECT_EQ(makeUniqueName("NIGHT", existing, 0), "NIGHT");
}

TEST(MakeUniqueName, SkipIndexDoesNotExcuseAnotherEntrysName) {
  const std::vector<std::string> existing = {"Night", "Sepia"};
  // Renaming "Sepia" (index 1) to "Night" still collides with index 0.
  EXPECT_EQ(makeUniqueName("Night", existing, 1), "Night 2");
}

TEST(MakeUniqueName, WithoutSkipIndexEveryExistingNameIsTaken) {
  const std::vector<std::string> existing = {"Night", "Sepia"};
  EXPECT_EQ(makeUniqueName("Night", existing, -1), "Night 2");
  EXPECT_EQ(makeUniqueName("Sepia", existing), "Sepia 2");
}
