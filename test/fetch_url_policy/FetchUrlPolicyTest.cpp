#include <gtest/gtest.h>

#include "FetchUrlPolicy.h"

namespace {

TEST(FetchUrlPolicy, TakesLastPathSegmentAsFilename) {
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/books/MyBook.epub"), "MyBook.epub");
}

TEST(FetchUrlPolicy, DropsQueryAndFragment) {
  EXPECT_EQ(fetch_url::filenameFromUrl("https://example.com/a/b.epub?token=abc&x=1"), "b.epub");
  EXPECT_EQ(fetch_url::filenameFromUrl("https://example.com/a/b.epub#chapter2"), "b.epub");
}

TEST(FetchUrlPolicy, DecodesPercentEscapes) {
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/My%20Book.epub"), "My Book.epub");
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/caf%C3%A9.epub"), "caf\xC3\xA9.epub");
}

TEST(FetchUrlPolicy, FallsBackWhenNoUsableSegment) {
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/"), "download");
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com"), "download");
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/books/"), "download");
  EXPECT_EQ(fetch_url::filenameFromUrl(""), "download");
}

TEST(FetchUrlPolicy, NeverEscapesTheDestinationDirectory) {
  // A percent-encoded separator must not survive decoding into the name.
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/a/%2E%2E%2Fetc%2Fpasswd"), "_etc_passwd");
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/a/.."), "download");
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/a/."), "download");
}

TEST(FetchUrlPolicy, ReadsQuotedContentDispositionFilename) {
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"book.epub\""), "book.epub");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=book.epub"), "book.epub");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("inline; FileName = \"book.epub\" ; x=1"), "book.epub");
}

TEST(FetchUrlPolicy, PrefersExtendedContentDispositionFilename) {
  EXPECT_EQ(
      fetch_url::filenameFromContentDisposition("attachment; filename=\"plain.epub\"; filename*=UTF-8''caf%C3%A9.epub"),
      "caf\xC3\xA9.epub");
}

TEST(FetchUrlPolicy, ReturnsEmptyForUnusableContentDisposition) {
  EXPECT_EQ(fetch_url::filenameFromContentDisposition(""), "");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment"), "");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"\""), "");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"..\""), "");
}

TEST(FetchUrlPolicy, KeepsSemicolonsInsideAQuotedFilename) {
  // RFC 6266 allows ';' inside a quoted-string; splitting on it blindly truncates
  // the name and loses the extension.
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"a;b.epub\""), "a;b.epub");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"a;b.epub\"; x=1"), "a;b.epub");
}

TEST(FetchUrlPolicy, RejectsNamesMadeOnlyOfControlCharacters) {
  // sanitizeFilename drops control characters and then substitutes "book" for an
  // empty result, which would bypass the caller's own fallback.
  EXPECT_EQ(fetch_url::filenameFromUrl("http://example.com/%01%02"), "download");
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"\x01\""), "");
}

TEST(FetchUrlPolicy, SanitizesContentDispositionFilename) {
  EXPECT_EQ(fetch_url::filenameFromContentDisposition("attachment; filename=\"/etc/passwd\""), "_etc_passwd");
}

TEST(FetchUrlPolicy, AcceptsOnlyHttpSchemes) {
  EXPECT_TRUE(fetch_url::isSupportedUrl("http://example.com/a.epub"));
  EXPECT_TRUE(fetch_url::isSupportedUrl("https://example.com/a.epub"));
  EXPECT_TRUE(fetch_url::isSupportedUrl("HTTPS://example.com/a.epub"));
  EXPECT_FALSE(fetch_url::isSupportedUrl("ftp://example.com/a.epub"));
  EXPECT_FALSE(fetch_url::isSupportedUrl("file:///etc/passwd"));
  EXPECT_FALSE(fetch_url::isSupportedUrl("example.com/a.epub"));
  EXPECT_FALSE(fetch_url::isSupportedUrl("http://"));
  EXPECT_FALSE(fetch_url::isSupportedUrl(""));
}

TEST(FetchUrlPolicy, CredentialsStayWithTheServerTheyWereGivenFor) {
  EXPECT_TRUE(fetch_url::mayCarryCredentials("https://books.example/opds", "https://books.example/get/1.epub"));
  EXPECT_TRUE(fetch_url::mayCarryCredentials("192.168.1.5:8083/opds", "http://192.168.1.5:8083/opds/new"));
  EXPECT_TRUE(fetch_url::mayCarryCredentials("http://Books.Example:8083/opds", "http://books.example:9000/1.epub"));
  // A server entered as http may answer with https links to itself.
  EXPECT_TRUE(fetch_url::mayCarryCredentials("http://books.example/opds", "https://books.example/1.epub"));
  EXPECT_TRUE(fetch_url::mayCarryCredentials("http://[fe80::1]:8083/opds", "http://[fe80::1]:8083/1.epub"));

  // A link the feed points at another host, or one that only starts like the server.
  EXPECT_FALSE(fetch_url::mayCarryCredentials("https://books.example/opds", "https://cdn.other.example/1.epub"));
  EXPECT_FALSE(fetch_url::mayCarryCredentials("https://books.example/opds", "https://books.example.evil.test/1.epub"));
  EXPECT_FALSE(fetch_url::mayCarryCredentials("https://books.example/opds", "https://books.example@evil.test/1"));
  EXPECT_FALSE(fetch_url::mayCarryCredentials("http://[fe80::1]/opds", "http://[fe80::2]/1.epub"));
  // Never from https down to plain http.
  EXPECT_FALSE(fetch_url::mayCarryCredentials("https://books.example/opds", "http://books.example/1.epub"));
  EXPECT_FALSE(fetch_url::mayCarryCredentials("", "http://books.example/1.epub"));
}

TEST(FetchUrlPolicy, JoinsDestinationPath) {
  EXPECT_EQ(fetch_url::destinationPath("/Books", "a.epub"), "/Books/a.epub");
  EXPECT_EQ(fetch_url::destinationPath("/Books/", "a.epub"), "/Books/a.epub");
  EXPECT_EQ(fetch_url::destinationPath("/", "a.epub"), "/a.epub");
  EXPECT_EQ(fetch_url::destinationPath("", "a.epub"), "/a.epub");
}

}  // namespace
