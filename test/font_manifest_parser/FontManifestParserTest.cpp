#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/FontManifestParser.h"

namespace {

// Trimmed to two families, but the same shape as the real fonts.json: a version, a
// baseUrl, a scriptGroups object the reader never looks at, and per-family "styles"
// and "scripts" arrays it never looks at either.
const char* kRealisticManifest = R"JSON({
  "version": 1,
  "baseUrl": "https://github.com/diogo7dias/lector-fonts/releases/download/sd-fonts-m1-b4/",
  "scriptGroups": {
    "latin": ["latin", "latin-ext"],
    "cyrillic": ["cyrillic"]
  },
  "families": [
    {
      "name": "Alegreya",
      "description": "Calligraphic serif/display (Latin, Greek, Cyrillic)",
      "styles": ["regular", "bold", "italic", "bolditalic"],
      "scripts": ["latin", "cyrillic", "greek"],
      "files": [
        {"name": "Alegreya_10.cpfont", "size": 474680, "crc32": 3569130992},
        {"name": "Alegreya_11.cpfont", "size": 537269, "crc32": 2455093511}
      ]
    },
    {
      "name": "Lora",
      "description": "Contemporary serif",
      "styles": ["regular", "bold"],
      "scripts": ["latin"],
      "files": [
        {"name": "Lora_14.cpfont", "size": 700000, "crc32": 1}
      ]
    }
  ]
})JSON";

void feedAll(FontManifestParser& parser, const char* json) {
  parser.feed(json, strlen(json));
  parser.finish();
}

// Feeds the document one byte at a time. The real source is an SD card file read in
// chunks, so a value split across two feeds has to survive.
void feedByteByByte(FontManifestParser& parser, const char* json) {
  for (const char* p = json; *p; p++) {
    parser.feed(p, 1);
  }
  parser.finish();
}

TEST(FontManifestParser, ParsesFamiliesAndFiles) {
  FontManifestParser parser;
  feedAll(parser, kRealisticManifest);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.version(), 1);
  EXPECT_STREQ(parser.baseUrl(), "https://github.com/diogo7dias/lector-fonts/releases/download/sd-fonts-m1-b4/");

  const auto& families = parser.families();
  ASSERT_EQ(families.size(), 2u);

  EXPECT_STREQ(families[0].name, "Alegreya");
  EXPECT_STREQ(families[0].description, "Calligraphic serif/display (Latin, Greek, Cyrillic)");
  ASSERT_EQ(families[0].files.size(), 2u);
  EXPECT_STREQ(families[0].files[0].name, "Alegreya_10.cpfont");
  EXPECT_EQ(families[0].files[0].size, 474680u);
  EXPECT_EQ(families[0].files[0].crc32, 3569130992u);
  EXPECT_EQ(families[0].files[1].crc32, 2455093511u);
  EXPECT_EQ(families[0].totalSize, 474680u + 537269u);

  EXPECT_STREQ(families[1].name, "Lora");
  ASSERT_EQ(families[1].files.size(), 1u);
  EXPECT_STREQ(families[1].files[0].name, "Lora_14.cpfont");
  EXPECT_EQ(families[1].totalSize, 700000u);
}

TEST(FontManifestParser, SurvivesChunkedFeeds) {
  FontManifestParser parser;
  feedByteByByte(parser, kRealisticManifest);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.families().size(), 2u);
  EXPECT_STREQ(parser.families()[0].files[1].name, "Alegreya_11.cpfont");
  EXPECT_EQ(parser.families()[1].files[0].size, 700000u);
}

TEST(FontManifestParser, IgnoresUnknownKeysAndContainers) {
  FontManifestParser parser;
  feedAll(parser, R"JSON({
    "version": 1,
    "extra": {"nested": {"deep": [1, 2, {"deeper": true}]}},
    "baseUrl": "https://example.com/f/",
    "families": [
      {
        "name": "Only",
        "unknownObject": {"a": 1},
        "unknownArray": [{"b": 2}],
        "files": [{"name": "Only_10.cpfont", "size": 10, "crc32": 7}]
      }
    ],
    "trailing": "ignored"
  })JSON");

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.families().size(), 1u);
  EXPECT_STREQ(parser.families()[0].name, "Only");
  EXPECT_STREQ(parser.families()[0].description, "");
  ASSERT_EQ(parser.families()[0].files.size(), 1u);
  EXPECT_EQ(parser.families()[0].files[0].crc32, 7u);
}

TEST(FontManifestParser, RejectsFileEntryWithoutCrc32) {
  FontManifestParser parser;
  feedAll(parser, R"JSON({
    "version": 1,
    "families": [
      {"name": "Broken", "files": [{"name": "Broken_10.cpfont", "size": 10}]}
    ]
  })JSON");

  EXPECT_TRUE(parser.hasError());
}

TEST(FontManifestParser, RejectsTruncatedDocument) {
  FontManifestParser parser;
  feedAll(parser, R"JSON({"version": 1, "families": [{"name": )JSON");

  EXPECT_TRUE(parser.hasError());
}

TEST(FontManifestParser, ReportsWrongVersionWithoutFamilies) {
  FontManifestParser parser;
  feedAll(parser, R"JSON({"version": 99, "families": []})JSON");

  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(parser.version(), 99);
  EXPECT_TRUE(parser.families().empty());
}

TEST(FontManifestParser, TruncatesOverlongStringsInsteadOfOverflowing) {
  const std::string longName(FontManifestFamily::MAX_NAME * 3, 'x');
  const std::string longDescription(FontManifestFamily::MAX_DESCRIPTION * 3, 'd');
  const std::string longFileName(FontManifestFile::MAX_NAME * 3, 'f');
  const std::string json = std::string(R"JSON({"version": 1, "families": [{"name": ")JSON") + longName +
                           R"JSON(", "description": ")JSON" + longDescription + R"JSON(", "files": [{"name": ")JSON" +
                           longFileName + R"JSON(", "size": 1, "crc32": 2}]}]})JSON";

  FontManifestParser parser;
  parser.feed(json.c_str(), json.size());
  parser.finish();

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.families().size(), 1u);
  EXPECT_EQ(strlen(parser.families()[0].name), FontManifestFamily::MAX_NAME - 1);
  EXPECT_EQ(strlen(parser.families()[0].description), FontManifestFamily::MAX_DESCRIPTION - 1);
  ASSERT_EQ(parser.families()[0].files.size(), 1u);
  EXPECT_EQ(strlen(parser.families()[0].files[0].name), FontManifestFile::MAX_NAME - 1);
}

TEST(FontManifestParser, StopsAtTheFamilyCapInsteadOfGrowingForever) {
  std::string json = R"JSON({"version": 1, "families": [)JSON";
  for (size_t i = 0; i < FontManifestParser::MAX_FAMILIES + 5; i++) {
    if (i) json += ",";
    json += R"JSON({"name": "F)JSON" + std::to_string(i) +
            R"JSON(", "files": [{"name": "f.cpfont", "size": 1, "crc32": 2}]})JSON";
  }
  json += "]}";

  FontManifestParser parser;
  parser.feed(json.c_str(), json.size());
  parser.finish();

  EXPECT_TRUE(parser.hasError());
  EXPECT_TRUE(parser.tooLarge());
  EXPECT_LE(parser.families().size(), FontManifestParser::MAX_FAMILIES);
}

TEST(FontManifestParser, StopsAtTheFileCapInsteadOfGrowingForever) {
  std::string json = R"JSON({"version": 1, "families": [{"name": "Big", "files": [)JSON";
  for (size_t i = 0; i < FontManifestParser::MAX_FILES_PER_FAMILY + 5; i++) {
    if (i) json += ",";
    json += R"JSON({"name": "f)JSON" + std::to_string(i) + R"JSON(.cpfont", "size": 1, "crc32": 2})JSON";
  }
  json += "]}]}";

  FontManifestParser parser;
  parser.feed(json.c_str(), json.size());
  parser.finish();

  EXPECT_TRUE(parser.hasError());
  EXPECT_TRUE(parser.tooLarge());
}

TEST(FontManifestParser, ResetClearsPreviousParse) {
  FontManifestParser parser;
  feedAll(parser, kRealisticManifest);
  ASSERT_EQ(parser.families().size(), 2u);

  parser.reset();
  EXPECT_TRUE(parser.families().empty());
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(parser.version(), 0);
  EXPECT_STREQ(parser.baseUrl(), "");

  feedAll(parser, R"JSON({"version": 1, "families": [{"name": "One", "files": []}]})JSON");
  ASSERT_EQ(parser.families().size(), 1u);
  EXPECT_STREQ(parser.families()[0].name, "One");
}

}  // namespace
