#include <gtest/gtest.h>

#include <set>
#include <string>

#include "lib/NearbyFileTransfer/NearbyFileRules.h"
#include "util/BookFilingNames.h"

namespace {

using namespace nearby_file;

// The firmware's own naming helper, which is what the activity passes in.
const CandidateNamer NAMER = [](const std::string_view name, const std::string_view folder, const int index) {
  return bookfiling::destinationCandidate(name, folder, index);
};

}  // namespace

TEST(NearbyFileRules, AcceptsTheFormatsThisReaderOpens) {
  for (const char* name :
       {"Book.epub", "notes.txt", "readme.md", "comic.xtc", "strip.xtch", "wallpaper.pxc", "cover.png", "sleep.bmp"}) {
    EXPECT_TRUE(isAcceptedFilename(name)) << name;
  }
}

TEST(NearbyFileRules, MatchesTheExtensionRegardlessOfCase) {
  EXPECT_TRUE(isAcceptedFilename("BOOK.EPUB"));
  EXPECT_TRUE(isAcceptedFilename("Wallpaper.PxC"));
}

TEST(NearbyFileRules, RejectsAnythingThisReaderCannotOpen) {
  for (const char* name : {"payload.bin", "settings.json", "firmware.bin", "archive.zip", "script.sh", "noextension",
                           "trailingdot.", ".epub", ""}) {
    EXPECT_FALSE(isAcceptedFilename(name)) << name;
  }
}

TEST(NearbyFileRules, StripsAnyPathFromAnOfferedName) {
  // The sender chooses this string. Nothing stops a hostile or broken device in
  // radio range from offering a name that walks out of the destination folder,
  // so the path is discarded and only the final component is kept.
  EXPECT_EQ(sanitizeFilename("../../.crosspoint/settings.json"), "settings.json");
  EXPECT_EQ(sanitizeFilename("/books/deep/Book.epub"), "Book.epub");
  EXPECT_EQ(sanitizeFilename("..\\..\\windows\\Book.epub"), "Book.epub");
  EXPECT_EQ(sanitizeFilename(".."), "");
  EXPECT_EQ(sanitizeFilename("."), "");
  EXPECT_EQ(sanitizeFilename("/"), "");
}

TEST(NearbyFileRules, RemovesCharactersThatAreNotSafeOnTheCard) {
  EXPECT_EQ(sanitizeFilename("Book\r\n.epub"), "Book.epub");
  EXPECT_EQ(sanitizeFilename(std::string("Book\0hidden.epub", 16)), "Bookhidden.epub");
  EXPECT_EQ(sanitizeFilename("Book*?:|<>.epub"), "Book.epub");
  // Ordinary punctuation a real book title carries stays intact.
  EXPECT_EQ(sanitizeFilename("Fear & Loathing (1971) - Thompson.epub"), "Fear & Loathing (1971) - Thompson.epub");
}

TEST(NearbyFileRules, CapsTheNameLengthWhileKeepingTheExtension) {
  const std::string longName = std::string(400, 'a') + ".epub";
  const std::string sanitized = sanitizeFilename(longName);

  EXPECT_LE(sanitized.size(), MAX_FILENAME_BYTES);
  EXPECT_EQ(sanitized.substr(sanitized.size() - 5), ".epub");
  EXPECT_TRUE(isAcceptedFilename(sanitized));
}

TEST(NearbyFileRules, RejectsAnOfferLargerThanTheFreeSpace) {
  // A margin is kept free: filling a FAT card to the last byte is how the
  // progress file and the section caches start failing to write.
  EXPECT_TRUE(fitsOnCard(1000, 1000 + FREE_SPACE_MARGIN_BYTES));
  EXPECT_FALSE(fitsOnCard(1001, 1000 + FREE_SPACE_MARGIN_BYTES));
  EXPECT_FALSE(fitsOnCard(1, 0));
  EXPECT_FALSE(fitsOnCard(0, 1000000));
  EXPECT_FALSE(fitsOnCard(MAX_TRANSFER_BYTES + 1, UINT64_MAX));
}

TEST(NearbyFileRules, NamesAroundAFileThatIsAlreadyThere) {
  const std::set<std::string> existing = {"/books/Book.epub", "/books/Book (2).epub"};
  const auto exists = [&existing](const std::string& path) { return existing.count(path) > 0; };

  EXPECT_EQ(resolveDestination("/books", "Book.epub", exists, NAMER), "/books/Book (3).epub");
  EXPECT_EQ(resolveDestination("/books", "Other.epub", exists, NAMER), "/books/Other.epub");
  // The card root is spelled as an empty folder, so a destination reads "/name".
  EXPECT_EQ(resolveDestination("", "Book.epub", exists, NAMER), "/Book.epub");
}

TEST(NearbyFileRules, GivesUpRatherThanLoopingForeverOnCollisions) {
  const auto everythingExists = [](const std::string&) { return true; };
  EXPECT_TRUE(resolveDestination("/books", "Book.epub", everythingExists, NAMER).empty());
}

TEST(NearbyFileRules, ValidatesAWholeOfferInOneCall) {
  OfferCheck check = checkOffer("Book.epub", 5000, 10000000);
  EXPECT_TRUE(check.accepted);
  EXPECT_EQ(check.safeName, "Book.epub");
  EXPECT_EQ(check.rejection, RejectReason::NONE);

  check = checkOffer("../secrets.json", 5000, 10000000);
  EXPECT_FALSE(check.accepted);
  EXPECT_EQ(check.rejection, RejectReason::UNSUPPORTED_TYPE);

  check = checkOffer("Book.epub", 900000000, 10000000);
  EXPECT_FALSE(check.accepted);
  EXPECT_EQ(check.rejection, RejectReason::TOO_LARGE);

  check = checkOffer("", 10, 10000000);
  EXPECT_FALSE(check.accepted);
  EXPECT_EQ(check.rejection, RejectReason::UNSUPPORTED_TYPE);

  // A name that sanitises down to a still-supported file is accepted under the
  // cleaned name, not the one that arrived.
  check = checkOffer("/books/../Book*.epub", 10, 10000000);
  EXPECT_TRUE(check.accepted);
  EXPECT_EQ(check.safeName, "Book.epub");
}

TEST(NearbyFileRules, AcceptsAFontFolderOnlyInTheFontsRoot) {
  EXPECT_EQ(sanitizeFontFolder(".fonts/Literata"), ".fonts/Literata");
  EXPECT_EQ(sanitizeFontFolder("/.fonts/Literata"), ".fonts/Literata");
  EXPECT_EQ(sanitizeFontFolder(".fonts/Noto_Sans-2"), ".fonts/Noto_Sans-2");

  // The sender picks this string, so every way of pointing it somewhere else has
  // to come back empty rather than be repaired into something writable.
  for (const char* folder :
       {"", ".fonts", ".fonts/", "fonts/Literata", ".fonts/../books", ".fonts/..", ".fonts/Sub/Deeper", "books",
        ".crosspoint", ".fonts/Lite rata", ".fonts/Lit.rata", ".fonts/.hidden", "..", "/"}) {
    EXPECT_TRUE(sanitizeFontFolder(folder).empty()) << folder;
  }
}

TEST(NearbyFileRules, NamesTheFamilyAFontFolderBelongsTo) {
  EXPECT_EQ(familyNameFromFolder(".fonts/Literata"), "Literata");
  EXPECT_TRUE(familyNameFromFolder("books").empty());
}

TEST(NearbyFileRules, TakesAFontFaceOnlyWhenItIsBoundForAFontFolder) {
  // A .cpfont is not a file the reader opens, so it is refused on its own. It is
  // allowed only as part of a family install, which is what the folder says.
  EXPECT_FALSE(isAcceptedFilename("Literata_14.cpfont"));

  OfferCheck check = checkOffer("Literata_14.cpfont", 51200, 10000000, ".fonts/Literata");
  EXPECT_TRUE(check.accepted);
  EXPECT_EQ(check.safeName, "Literata_14.cpfont");
  EXPECT_EQ(check.safeFolder, ".fonts/Literata");

  // A face with no folder, and a book with a font folder, are both refused: the
  // two only travel together.
  EXPECT_FALSE(checkOffer("Literata_14.cpfont", 51200, 10000000, "").accepted);
  EXPECT_FALSE(checkOffer("Book.epub", 51200, 10000000, ".fonts/Literata").accepted);
  EXPECT_FALSE(checkOffer("Literata_14.cpfont", 51200, 10000000, ".fonts/../books").accepted);
  EXPECT_FALSE(checkOffer("Literata.14.cpfont", 51200, 10000000, ".fonts/Literata").accepted);
  EXPECT_EQ(checkOffer("Literata_14.cpfont", 51200, 10000000, "").rejection, RejectReason::UNSUPPORTED_TYPE);
}

TEST(NearbyFileRules, StillChecksTheSizeOfAFontFace) {
  const OfferCheck check = checkOffer("Literata_14.cpfont", 900000000, 10000000, ".fonts/Literata");
  EXPECT_FALSE(check.accepted);
  EXPECT_EQ(check.rejection, RejectReason::TOO_LARGE);
}

TEST(NearbyFileRules, LeavesABookOfferUnchangedWhenNoFolderIsNamed) {
  const OfferCheck check = checkOffer("Book.epub", 5000, 10000000, "");
  EXPECT_TRUE(check.accepted);
  EXPECT_TRUE(check.safeFolder.empty());
}
