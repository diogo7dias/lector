#include <OpdsParser.h>
#include <gtest/gtest.h>

#include <string>

namespace {

std::string feedWith(const int entryCount, const size_t titleChars) {
  std::string feed = "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\">";
  feed += "<link rel=\"next\" href=\"/opds/discover/2\" type=\"application/atom+xml\"/>";
  for (int i = 0; i < entryCount; ++i) {
    feed += "<entry><title>";
    feed += std::string(titleChars, 'x');
    feed += std::to_string(i);
    feed +=
        "</title><author><name>Author</name></author>"
        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
        "href=\"/opds/download/";
    feed += std::to_string(i);
    feed += "/epub/\"/></entry>";
  }
  feed += "</feed>";
  return feed;
}

// Feeds in chunks, the way the downloader does.
void feedInChunks(OpdsParser& parser, const std::string& xml) {
  constexpr size_t chunk = 512;
  for (size_t off = 0; off < xml.size(); off += chunk) {
    const size_t len = std::min(chunk, xml.size() - off);
    parser.write(reinterpret_cast<const uint8_t*>(xml.data() + off), len);
  }
  parser.flush();
}

TEST(OpdsParserTest, ParsesEntriesAndFeedLinks) {
  OpdsParser parser;
  feedInChunks(parser, feedWith(3, 10));

  EXPECT_FALSE(parser.error());
  EXPECT_FALSE(parser.truncated());
  EXPECT_EQ(parser.getNextPageUrl(), "/opds/discover/2");

  const auto entries = parser.takeEntries();
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].title, std::string(10, 'x') + "0");
  EXPECT_EQ(entries[0].author, "Author");
  EXPECT_EQ(entries[2].href, "/opds/download/2/epub/");
}

// The reason this parser exists in its current shape: a feed far bigger than
// the board can hold must stop growing, not eat the heap out from under the
// TLS session that is still delivering it.
TEST(OpdsParserTest, TruncatesOversizedFeedInsteadOfGrowing) {
  OpdsParser parser;
  // 400 entries of ~150 title chars: well past both the entry cap and the arena.
  feedInChunks(parser, feedWith(400, 150));

  EXPECT_FALSE(parser.error());
  EXPECT_TRUE(parser.truncated());

  const auto entries = parser.takeEntries();
  EXPECT_LE(entries.size(), 64u);
  EXPECT_GT(entries.size(), 0u);
  for (const auto& entry : entries) {
    EXPECT_FALSE(entry.title.empty());
    EXPECT_FALSE(entry.href.empty());
  }
}

TEST(OpdsParserTest, TakeEntriesLeavesParserEmpty) {
  OpdsParser parser;
  feedInChunks(parser, feedWith(2, 5));

  EXPECT_EQ(parser.entryCount(), 2u);
  EXPECT_EQ(parser.takeEntries().size(), 2u);
  EXPECT_EQ(parser.entryCount(), 0u);
  EXPECT_TRUE(parser.takeEntries().empty());
}

}  // namespace
