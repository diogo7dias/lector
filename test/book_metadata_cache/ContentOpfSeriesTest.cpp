#include <gtest/gtest.h>

#include <string>

#include "Epub/parsers/ContentOpfParser.h"

namespace {

void feedOpf(ContentOpfParser& parser, const std::string& xml) {
  parser.metadataOnly = true;
  ASSERT_TRUE(parser.setup());
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
}

TEST(ContentOpfSeriesTest, ParsesCalibreSeriesAndIndex) {
  const std::string opf =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\">"
      "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "    <dc:title>Dune Messiah</dc:title>"
      "    <dc:creator>Frank Herbert</dc:creator>"
      "    <meta name=\"calibre:series\" content=\"Dune\"/>"
      "    <meta name=\"calibre:series_index\" content=\"2.5\"/>"
      "  </metadata>"
      "</package>";

  ContentOpfParser parser("/cache", "", opf.size(), nullptr);
  feedOpf(parser, opf);
  EXPECT_EQ(parser.title, "Dune Messiah");
  EXPECT_EQ(parser.author, "Frank Herbert");
  EXPECT_EQ(parser.series, "Dune");
  EXPECT_EQ(parser.seriesIndexX10, 25);
}

TEST(ContentOpfSeriesTest, ParsesEpub3BelongsToCollectionAndGroupPosition) {
  const std::string opf =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
      "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "    <dc:title>The Fellowship of the Ring</dc:title>"
      "    <dc:creator>J. R. R. Tolkien</dc:creator>"
      "    <meta property=\"belongs-to-collection\" id=\"c01\">The Lord of the Rings</meta>"
      "    <meta refines=\"#c01\" property=\"collection-type\">series</meta>"
      "    <meta refines=\"#c01\" property=\"group-position\">1</meta>"
      "  </metadata>"
      "</package>";

  ContentOpfParser parser("/cache", "", opf.size(), nullptr);
  feedOpf(parser, opf);
  EXPECT_EQ(parser.series, "The Lord of the Rings");
  EXPECT_EQ(parser.seriesIndexX10, 10);
}

TEST(ContentOpfSeriesTest, ParseSeriesIndexHandlesEdgeCases) {
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10("1"), 10);
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10("02.5"), 25);
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10("3.26"), 33);
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10("-1"), 0);
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10("abc"), 0);
  EXPECT_EQ(ContentOpfParser::parseSeriesIndexX10(""), 0);
}

}  // namespace
