// Host tests for the favorite-wallpaper filename suffix model.
//
// The filename IS the favorite state, so these helpers decide whether a user's
// wallpaper counts as a favorite and what it gets renamed to. A wrong answer
// here renames the wrong file on the SD card.
#include <gtest/gtest.h>

#include "sleep/WallpaperNames.h"
#include "util/FavoriteImageNames.h"

using crosspoint::sleep::isWallpaperName;
using FavoriteImage::addFavoriteSuffix;
using FavoriteImage::hasFavoriteSuffix;
using FavoriteImage::isImageExtension;
using FavoriteImage::stripFavoriteSuffix;

TEST(IsImageExtension, AcceptsWallpaperExtensions) {
  EXPECT_TRUE(isImageExtension("a.bmp"));
  EXPECT_TRUE(isImageExtension("a.pxc"));
}

TEST(IsImageExtension, IsCaseInsensitive) {
  EXPECT_TRUE(isImageExtension("A.BMP"));
  EXPECT_TRUE(isImageExtension("A.PxC"));
}

TEST(IsImageExtension, RejectsEverythingElse) {
  EXPECT_FALSE(isImageExtension("book.epub"));
  EXPECT_FALSE(isImageExtension("a.png"));
  EXPECT_FALSE(isImageExtension("notes.txt"));
  EXPECT_FALSE(isImageExtension(""));
}

TEST(IsImageExtension, RejectsNameEndingInExtensionLettersWithoutTheDot) {
  EXPECT_FALSE(isImageExtension("abmp"));
  EXPECT_FALSE(isImageExtension("scrapbmp"));
}

TEST(IsWallpaperName, AcceptsOrdinaryWallpapers) {
  EXPECT_TRUE(isWallpaperName("sunset.bmp"));
  EXPECT_TRUE(isWallpaperName("sunset_F.pxc"));
}

TEST(IsWallpaperName, RejectsDotfiles) {
  // macOS writes "._name.bmp" resource forks all over a card, and a bare ".bmp"
  // is a hidden file, not a wallpaper. Neither should ever be shown or moved.
  EXPECT_FALSE(isWallpaperName("._sunset.bmp"));
  EXPECT_FALSE(isWallpaperName(".bmp"));
  EXPECT_FALSE(isWallpaperName(""));
}

TEST(IsWallpaperName, RejectsOtherFileTypes) {
  EXPECT_FALSE(isWallpaperName("book.epub"));
  EXPECT_FALSE(isWallpaperName("cover.png"));
}

TEST(HasFavoriteSuffix, DetectsSuffix) {
  EXPECT_TRUE(hasFavoriteSuffix("sunset_F.bmp"));
  EXPECT_TRUE(hasFavoriteSuffix("sunset_F.pxc"));
  EXPECT_FALSE(hasFavoriteSuffix("sunset.bmp"));
}

TEST(HasFavoriteSuffix, IsCaseSensitiveOnTheSuffix) {
  // Only "_F" marks a favorite. "_f" is an ordinary filename, and treating it as
  // a favorite would silently rename files the user never starred.
  EXPECT_FALSE(hasFavoriteSuffix("sunset_f.bmp"));
}

TEST(HasFavoriteSuffix, RequiresRealNameBeforeSuffix) {
  // "_F.bmp" is the suffix and nothing else — not a favorited file.
  EXPECT_FALSE(hasFavoriteSuffix("_F.bmp"));
  EXPECT_TRUE(hasFavoriteSuffix("a_F.bmp"));
}

TEST(HasFavoriteSuffix, IgnoresSuffixNotAdjacentToExtension) {
  EXPECT_FALSE(hasFavoriteSuffix("sunset_F_2.bmp"));
  EXPECT_FALSE(hasFavoriteSuffix("_Fsunset.bmp"));
}

TEST(HasFavoriteSuffix, RejectsNonWallpaperFiles) { EXPECT_FALSE(hasFavoriteSuffix("book_F.epub")); }

TEST(AddFavoriteSuffix, InsertsBeforeExtension) {
  EXPECT_EQ("sunset_F.bmp", addFavoriteSuffix("sunset.bmp"));
  EXPECT_EQ("sunset_F.PXC", addFavoriteSuffix("sunset.PXC"));
}

TEST(AddFavoriteSuffix, IsIdempotent) { EXPECT_EQ("sunset_F.bmp", addFavoriteSuffix("sunset_F.bmp")); }

TEST(AddFavoriteSuffix, LeavesNonWallpapersAlone) { EXPECT_EQ("book.epub", addFavoriteSuffix("book.epub")); }

TEST(StripFavoriteSuffix, RemovesSuffix) {
  EXPECT_EQ("sunset.bmp", stripFavoriteSuffix("sunset_F.bmp"));
  EXPECT_EQ("sunset.pxc", stripFavoriteSuffix("sunset_F.pxc"));
}

TEST(StripFavoriteSuffix, IsIdempotent) { EXPECT_EQ("sunset.bmp", stripFavoriteSuffix("sunset.bmp")); }

TEST(StripFavoriteSuffix, LeavesInnerUnderscoreFAlone) { EXPECT_EQ("a_F_b.bmp", stripFavoriteSuffix("a_F_b.bmp")); }

TEST(RoundTrip, AddThenStripRestoresOriginal) {
  const std::string original = "my sunset photo.bmp";
  EXPECT_EQ(original, stripFavoriteSuffix(addFavoriteSuffix(original)));
}
