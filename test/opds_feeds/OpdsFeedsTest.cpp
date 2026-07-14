// Host tests for the OPDS pipeline: the firmware's OpdsParser over real
// calibre-web / Calibre-Web-Automated feeds, plus UrlUtils::buildUrl href
// resolution. These are the two host-testable layers of the OPDS flow
// (HttpDownloader is esp_http_client and device-only).
#include <gtest/gtest.h>

#include <string>

#include "FeedFixtures.h"
#include "OpdsParser.h"
#include "util/UrlUtils.h"

namespace {

void parseOk(OpdsParser& parser, const char* xml) {
  const size_t len = strlen(xml);
  parser.write(reinterpret_cast<const uint8_t*>(xml), len);
  parser.flush();
  EXPECT_FALSE(parser.error());
}

std::string wrapFeed(const std::string& entries) {
  return R"(<?xml version="1.0" encoding="UTF-8"?>)"
         "\n<feed xmlns=\"http://www.w3.org/2005/Atom\">" +
         entries + "</feed>";
}

// ---------------------------------------------------------------------------
// Real captured feeds (calibre-web 0.6.26, the engine inside CWA)
// ---------------------------------------------------------------------------

TEST(OpdsRealFeeds, RootFeedParsesNavigationAndSearch) {
  OpdsParser parser;
  parseOk(parser, kRootFeed);
  EXPECT_EQ(parser.getSearchTemplate(), "/opds/search/{searchTerms}");
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 15u);
  for (const auto& e : entries) {
    EXPECT_EQ(e.type, OpdsEntryType::NAVIGATION);
    EXPECT_FALSE(e.href.empty());
  }
  EXPECT_EQ(entries[0].title, "Alphabetical Books");
  EXPECT_EQ(entries[0].href, "/opds/books");
}

TEST(OpdsRealFeeds, BookFeedParsesBooksWithAuthorsAndDownloadHrefs) {
  OpdsParser parser;
  parseOk(parser, kNewBooksFeed);
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].title, "Test Book One");
  EXPECT_EQ(entries[0].author, "Alpha Author");
  EXPECT_EQ(entries[0].href, "/opds/download/1/epub/");
  EXPECT_EQ(entries[1].title, "Second Sample");
  EXPECT_EQ(entries[1].href, "/opds/download/2/epub/");
}

TEST(OpdsRealFeeds, AuthorIndexFeedParsesLetterNavigation) {
  OpdsParser parser;
  parseOk(parser, kAuthorFeed);
  const auto& entries = parser.getEntries();
  ASSERT_GE(entries.size(), 2u);
  for (const auto& e : entries) {
    EXPECT_EQ(e.type, OpdsEntryType::NAVIGATION);
  }
}

// ---------------------------------------------------------------------------
// CWA-specific entry shape (uses <summary> instead of <content>, magic
// shelves) and real-world server variations
// ---------------------------------------------------------------------------

TEST(OpdsVariants, CwaSummaryBookEntryParses) {
  const std::string xml = wrapFeed(R"(
  <entry>
    <title>CWA Book</title>
    <id>urn:uuid:1234</id>
    <author><name>Some Writer</name></author>
    <summary>Plot text stripped of tags</summary>
    <link type="image/jpeg" href="/opds/cover/9" rel="http://opds-spec.org/image"/>
    <link rel="http://opds-spec.org/acquisition" href="/opds/download/9/epub/"
          length="1000" mtime="2026-01-01T00:00:00+00:00" type="application/epub+zip"/>
  </entry>)");
  OpdsParser parser;
  parseOk(parser, xml.c_str());
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].title, "CWA Book");
  EXPECT_EQ(entries[0].author, "Some Writer");
  EXPECT_EQ(entries[0].href, "/opds/download/9/epub/");
}

TEST(OpdsVariants, EpubTypeWithMimeParametersIsAccepted) {
  // Some servers append MIME parameters to the acquisition type. The entry
  // must still be recognized as a downloadable EPUB.
  const std::string xml = wrapFeed(R"(
  <entry>
    <title>Param Book</title>
    <id>x</id>
    <link rel="http://opds-spec.org/acquisition"
          href="/get/epub/12"
          type="application/epub+zip;profile=opds"/>
  </entry>)");
  OpdsParser parser;
  parseOk(parser, xml.c_str());
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].href, "/get/epub/12");
}

TEST(OpdsVariants, KepubNotMistakenForEpub) {
  // application/kepub+zip must not be picked up as an EPUB acquisition.
  const std::string xml = wrapFeed(R"(
  <entry>
    <title>Kobo Book</title>
    <id>x</id>
    <link rel="http://opds-spec.org/acquisition" href="/get/kepub/12"
          type="application/kepub+zip"/>
  </entry>)");
  OpdsParser parser;
  parseOk(parser, xml.c_str());
  EXPECT_TRUE(parser.getEntries().empty());
}

TEST(OpdsVariants, PlainEpubPreferredOverDerivedFormats) {
  const std::string xml = wrapFeed(R"(
  <entry>
    <title>Multi Format</title>
    <id>x</id>
    <link rel="http://opds-spec.org/acquisition" href="/get/12/converted"
          type="application/epub+zip"/>
    <link rel="http://opds-spec.org/acquisition" href="/get/12/book.epub"
          type="application/epub+zip"/>
  </entry>)");
  OpdsParser parser;
  parseOk(parser, xml.c_str());
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].href, "/get/12/book.epub");
}

TEST(OpdsVariants, NextAndPrevPaginationLinks) {
  const std::string xml = wrapFeed(R"(
  <link rel="next" href="/opds/new?offset=60" type="application/atom+xml;profile=opds-catalog"/>
  <link rel="previous" href="/opds/new?offset=0" type="application/atom+xml;profile=opds-catalog"/>
  <entry>
    <title>N</title><id>x</id>
    <link href="/opds/sub" type="application/atom+xml;profile=opds-catalog"/>
  </entry>)");
  OpdsParser parser;
  parseOk(parser, xml.c_str());
  EXPECT_EQ(parser.getNextPageUrl(), "/opds/new?offset=60");
  EXPECT_EQ(parser.getPrevPageUrl(), "/opds/new?offset=0");
}

// ---------------------------------------------------------------------------
// UrlUtils::buildUrl — href resolution used by fetchFeed/navigate/download
// ---------------------------------------------------------------------------

TEST(BuildUrl, AbsoluteUrlPassesThrough) {
  EXPECT_EQ(UrlUtils::buildUrl("https://h:8450/opds", "https://other/feed"), "https://other/feed");
}

TEST(BuildUrl, EmptyPathReturnsServer) {
  EXPECT_EQ(UrlUtils::buildUrl("https://h:8450/opds", ""), "https://h:8450/opds");
}

TEST(BuildUrl, AbsolutePathReplacesPathKeepingHostAndPort) {
  // calibre-web / CWA hrefs are all absolute paths; the port must survive.
  EXPECT_EQ(UrlUtils::buildUrl("https://kumedia.duckdns.org:8450/opds", "/opds/download/1/epub/"),
            "https://kumedia.duckdns.org:8450/opds/download/1/epub/");
}

TEST(BuildUrl, SchemeAddedWhenMissing) { EXPECT_EQ(UrlUtils::buildUrl("h:8080/opds", "/x"), "http://h:8080/x"); }

TEST(BuildUrl, RelativeHrefResolvesAgainstParentPerRfc3986) {
  // RFC 3986 5.3: a relative reference replaces the base URL's last path
  // segment. A feed at /opds/new linking "page2" means /opds/page2.
  EXPECT_EQ(UrlUtils::buildUrl("http://h/opds/new", "page2"), "http://h/opds/page2");
}

TEST(BuildUrl, RelativeHrefAgainstTrailingSlashAppends) {
  EXPECT_EQ(UrlUtils::buildUrl("http://h/opds/", "sub"), "http://h/opds/sub");
}

TEST(BuildUrl, RelativeHrefAgainstBareHostAppends) {
  EXPECT_EQ(UrlUtils::buildUrl("http://h", "feed.xml"), "http://h/feed.xml");
}

TEST(BuildUrl, QueryStringStrippedBeforeResolving) {
  EXPECT_EQ(UrlUtils::buildUrl("http://h/opds/new?offset=60", "page2"), "http://h/opds/page2");
}

}  // namespace
