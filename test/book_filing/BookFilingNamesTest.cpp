#include <gtest/gtest.h>

#include "util/BookFilingNames.h"

using namespace bookfiling;

TEST(IsInFolder, MatchesADirectChild) {
  EXPECT_TRUE(isInFolder("/recents/My Book.epub", RECENTS_FOLDER));
  EXPECT_TRUE(isInFolder("/read/Done.epub", READ_FOLDER));
}

TEST(IsInFolder, RejectsTheRootAndOtherFolders) {
  EXPECT_FALSE(isInFolder("/My Book.epub", RECENTS_FOLDER));
  EXPECT_FALSE(isInFolder("/read/Done.epub", RECENTS_FOLDER));
}

TEST(IsInFolder, RejectsAFolderNameThatIsOnlyAPrefix) {
  // "/recentsish/x.epub" starts with "/recents" but is a different folder.
  EXPECT_FALSE(isInFolder("/recentsish/x.epub", RECENTS_FOLDER));
  EXPECT_FALSE(isInFolder("/recents", RECENTS_FOLDER));
}

TEST(FileNameOf, TakesTheLastSegment) {
  EXPECT_EQ(fileNameOf("/recents/My Book.epub"), "My Book.epub");
  EXPECT_EQ(fileNameOf("plain.epub"), "plain.epub");
}

TEST(DestinationCandidate, FirstCandidateKeepsTheName) {
  EXPECT_EQ(destinationCandidate("/My Book.epub", RECENTS_FOLDER, 1), "/recents/My Book.epub");
}

TEST(DestinationCandidate, LaterCandidatesSuffixBeforeTheExtension) {
  EXPECT_EQ(destinationCandidate("/My Book.epub", RECENTS_FOLDER, 2), "/recents/My Book (2).epub");
  EXPECT_EQ(destinationCandidate("/recents/My Book.epub", ROOT_FOLDER, 3), "/My Book (3).epub");
}

TEST(DestinationCandidate, RootFolderYieldsATopLevelPath) {
  EXPECT_EQ(destinationCandidate("/recents/My Book.epub", ROOT_FOLDER, 1), "/My Book.epub");
}

TEST(DestinationCandidate, ANameWithNoExtensionTakesTheSuffixAtTheEnd) {
  EXPECT_EQ(destinationCandidate("/notes", RECENTS_FOLDER, 2), "/recents/notes (2)");
}

TEST(DestinationCandidate, IndexZeroBehavesLikeTheFirstCandidate) {
  EXPECT_EQ(destinationCandidate("/My Book.epub", ROOT_FOLDER, 0), "/My Book.epub");
}

TEST(CacheDirFor, PrefixFollowsTheBookFormat) {
  EXPECT_EQ(cacheDirFor("/a.epub").rfind("/.crosspoint/epub_", 0), 0u);
  EXPECT_EQ(cacheDirFor("/a.txt").rfind("/.crosspoint/txt_", 0), 0u);
  EXPECT_EQ(cacheDirFor("/a.md").rfind("/.crosspoint/txt_", 0), 0u);  // .md is read by the txt reader
  EXPECT_EQ(cacheDirFor("/a.xtc").rfind("/.crosspoint/xtc_", 0), 0u);
}

TEST(CacheDirFor, ExtensionCaseDoesNotChangeThePrefix) {
  EXPECT_EQ(cacheDirFor("/a.EPUB").rfind("/.crosspoint/epub_", 0), 0u);
  // The hash still covers the raw path, matching Epub's own formula, so a differently
  // cased name is a different book with a different cache dir.
  EXPECT_NE(cacheDirFor("/a.EPUB"), cacheDirFor("/a.epub"));
}

TEST(CacheDirFor, UnknownExtensionHasNoCacheDir) {
  EXPECT_TRUE(cacheDirFor("/a.pxc").empty());
  EXPECT_TRUE(cacheDirFor("/a").empty());
}

TEST(CacheDirFor, MovingABookChangesItsCacheDir) {
  // This is why a move must re-key the cache: same name, different folder, different key.
  EXPECT_NE(cacheDirFor("/recents/My Book.epub"), cacheDirFor("/My Book.epub"));
}

TEST(DisplayNameFor, PrefersTheTitle) { EXPECT_EQ(displayNameFor("Moby Dick", "/recents/moby.epub"), "Moby Dick"); }

TEST(DisplayNameFor, TrimsThePrettyPrintedWhitespaceAroundATitle) {
  EXPECT_EQ(displayNameFor("\n    Moby Dick\n  ", "/moby.epub"), "Moby Dick");
}

TEST(DisplayNameFor, FallsBackToTheFileNameWhenTheTitleIsEmptyOrBlank) {
  EXPECT_EQ(displayNameFor("", "/recents/moby.epub"), "moby.epub");
  EXPECT_EQ(displayNameFor("   \n\t ", "/recents/moby.epub"), "moby.epub");
}

TEST(DisplayNameFor, FallsBackToTheWholePathWhenThereIsNoSlash) {
  EXPECT_EQ(displayNameFor("", "moby.epub"), "moby.epub");
}
