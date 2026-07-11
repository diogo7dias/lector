#include <gtest/gtest.h>

#include <array>

#include "FontUploadPolicy.h"

namespace {
std::array<uint8_t, 92> validFile() {
  std::array<uint8_t, 92> header{};
  const uint8_t magic[] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};
  std::copy(std::begin(magic), std::end(magic), header.begin());
  header[8] = 4;
  header[12] = 1;
  header[32] = 0;
  header[36] = 1;  // interval count
  header[40] = 1;  // glyph count
  header[56] = 64;  // style data offset
  header[64] = 32;  // interval first
  header[68] = 32;  // interval last
  return header;
}
}  // namespace

TEST(FontUploadPolicy, AcceptsHeaderSplitAcrossChunks) {
  FontUploadPolicy policy;
  const auto header = validFile();
  policy.add(header.data(), 3);
  policy.add(header.data() + 3, 5);
  policy.add(header.data() + 8, header.size() - 8);
  EXPECT_TRUE(policy.finish(header.size()));
}

TEST(FontUploadPolicy, RejectsShortAndSizeMismatchedFiles) {
  FontUploadPolicy shortFile;
  const auto header = validFile();
  shortFile.add(header.data(), 7);
  EXPECT_FALSE(shortFile.finish(7));

  FontUploadPolicy truncated;
  truncated.add(header.data(), header.size());
  EXPECT_FALSE(truncated.finish(header.size() + 1));
}

TEST(FontUploadPolicy, RejectsBadMagicVersionAndStyleCount) {
  auto header = validFile();
  header[0] = 'X';
  FontUploadPolicy badMagic;
  badMagic.add(header.data(), header.size());
  EXPECT_FALSE(badMagic.finish(header.size()));

  header = validFile();
  header[8] = 3;
  FontUploadPolicy badVersion;
  badVersion.add(header.data(), header.size());
  EXPECT_FALSE(badVersion.finish(header.size()));

  header = validFile();
  header[12] = 0;
  FontUploadPolicy badStyles;
  badStyles.add(header.data(), header.size());
  EXPECT_FALSE(badStyles.finish(header.size()));
}

TEST(FontUploadPolicy, RejectsMissingTocDuplicateStylesAndOutOfBoundsSections) {
  auto file = validFile();
  FontUploadPolicy missingToc;
  missingToc.add(file.data(), 32);
  EXPECT_FALSE(missingToc.finish(32));

  std::array<uint8_t, 124> duplicateFile{};
  std::copy_n(file.begin(), 64, duplicateFile.begin());
  duplicateFile[12] = 2;
  duplicateFile[64] = 0;  // duplicate style id in second TOC
  duplicateFile[68] = 1;
  duplicateFile[72] = 1;
  duplicateFile[88] = 96;
  FontUploadPolicy duplicateStyle;
  duplicateStyle.add(duplicateFile.data(), duplicateFile.size());
  EXPECT_FALSE(duplicateStyle.finish(duplicateFile.size()));

  file = validFile();
  file[56] = 90;  // fixed interval + glyph data extends past EOF
  FontUploadPolicy outOfBounds;
  outOfBounds.add(file.data(), file.size());
  EXPECT_FALSE(outOfBounds.finish(file.size()));
}

TEST(FontUploadPolicy, EnforcesNamesAndFamilyCap) {
  EXPECT_TRUE(font_upload::lengthsAreSafe(63, 120));
  EXPECT_FALSE(font_upload::lengthsAreSafe(64, 120));
  EXPECT_FALSE(font_upload::lengthsAreSafe(63, 121));
  EXPECT_TRUE(font_upload::canInstallFamily(128, true));
  EXPECT_FALSE(font_upload::canInstallFamily(128, false));
  EXPECT_TRUE(font_upload::pathLengthIsSafe(127));
  EXPECT_FALSE(font_upload::pathLengthIsSafe(128));
}

TEST(FontUploadPolicy, RejectsCorruptGlyphBitmapLengthAndOffset) {
  std::array<uint8_t, 16> glyph{};
  glyph[0] = 4;
  glyph[1] = 4;
  glyph[8] = 4;  // 16 pixels at 2 bits per pixel
  EXPECT_TRUE(font_upload::glyphRecordIsSafe(glyph.data(), true, 0, 4));

  glyph[8] = 3;
  EXPECT_FALSE(font_upload::glyphRecordIsSafe(glyph.data(), true, 0, 4));
  glyph[8] = 4;
  glyph[12] = 1;
  EXPECT_FALSE(font_upload::glyphRecordIsSafe(glyph.data(), true, 0, 5));
  glyph[12] = 0;
  EXPECT_FALSE(font_upload::glyphRecordIsSafe(glyph.data(), true, 0, 3));
}
