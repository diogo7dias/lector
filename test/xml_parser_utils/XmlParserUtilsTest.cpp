#include <gtest/gtest.h>

#include <string>

#include "XmlParserUtils.h"

TEST(XmlParserUtils, MatchesLocalNamesRegardlessOfPrefix) {
  EXPECT_TRUE(xmlLocalNameEquals("package", "package"));
  EXPECT_TRUE(xmlLocalNameEquals("ns0:package", "package"));
  EXPECT_FALSE(xmlLocalNameEquals("ns0:metadata", "package"));
}

namespace {
void XMLCALL countElement(void* userData, const XML_Char*, const XML_Char**) { ++*static_cast<int*>(userData); }
}  // namespace

// A document split over several writes, each larger than one 1KB feed chunk,
// parses whole: only the chunk that ends the document is flagged final.
TEST(XmlParserUtils, FeedParsesDocumentAcrossWritesAndChunks) {
  std::string doc = "<root>";
  for (int i = 0; i < 300; i++) doc += "<item/>";
  doc += "</root>";

  XML_Parser parser = XML_ParserCreate(nullptr);
  int elements = 0;
  XML_SetUserData(parser, &elements);
  XML_SetElementHandler(parser, countElement, nullptr);

  size_t remaining = doc.size();
  const auto* bytes = reinterpret_cast<const uint8_t*>(doc.data());
  const size_t split = 1500;
  EXPECT_EQ(feedXmlParser(parser, bytes, split, remaining, "T"), split);
  EXPECT_EQ(feedXmlParser(parser, bytes + split, doc.size() - split, remaining, "T"), doc.size() - split);
  EXPECT_EQ(remaining, 0u);
  EXPECT_EQ(elements, 301);
  destroyXmlParser(parser);
}

// Malformed input destroys the parser and every later write reports 0.
TEST(XmlParserUtils, FeedDestroysParserOnError) {
  const std::string doc = "<root><a></b></root>";
  XML_Parser parser = XML_ParserCreate(nullptr);
  size_t remaining = doc.size();
  const auto* bytes = reinterpret_cast<const uint8_t*>(doc.data());
  EXPECT_EQ(feedXmlParser(parser, bytes, doc.size(), remaining, "T"), 0u);
  EXPECT_EQ(parser, nullptr);
  EXPECT_EQ(feedXmlParser(parser, bytes, doc.size(), remaining, "T"), 0u);
}
