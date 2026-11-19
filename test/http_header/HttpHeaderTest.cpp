#include <HttpHeader.h>
#include <gtest/gtest.h>

#include <string>

#include "HttpRequestExtras.h"

TEST(HttpHeaderTest, ParsesTrimmedNameAndValue) {
  HttpHeader header;
  parseHttpHeaderLine("  CF-Access-Client-Id :\t abc123  ", header);
  EXPECT_EQ(header.name, "CF-Access-Client-Id");
  EXPECT_EQ(header.value, "abc123");
}

TEST(HttpHeaderTest, SplitsOnFirstColonOnly) {
  HttpHeader header;
  parseHttpHeaderLine("X-Target: http://host:8080", header);
  EXPECT_EQ(header.name, "X-Target");
  EXPECT_EQ(header.value, "http://host:8080");
}

TEST(HttpHeaderTest, MissingColonIsBareNameAndClearsValue) {
  HttpHeader header{"Old", "stale"};
  parseHttpHeaderLine(" X-Flag ", header);
  EXPECT_EQ(header.name, "X-Flag");
  EXPECT_EQ(header.value, "");
}

TEST(HttpHeaderTest, BlankLineClearsHeader) {
  HttpHeader header{"Old", "stale"};
  parseHttpHeaderLine("   ", header);
  EXPECT_EQ(header.name, "");
  EXPECT_EQ(header.value, "");
}

TEST(HttpHeaderTest, FormatsAndMasks) {
  EXPECT_EQ(formatHttpHeaderLine({"", "orphan"}), "");
  EXPECT_EQ(formatHttpHeaderLine({"X-Flag", ""}), "X-Flag");
  EXPECT_EQ(formatHttpHeaderLine({"X-Key", "secret"}), "X-Key: secret");
  EXPECT_EQ(formatHttpHeaderLine({"X-Key", "secret"}, true), "X-Key: ******");
  EXPECT_EQ(formatHttpHeaderLine({"X-Flag", ""}, true), "X-Flag");
}

TEST(HttpHeaderTest, FormatRoundTripsThroughParse) {
  const HttpHeader original{"Authorization", "Bearer a:b"};
  HttpHeader parsed;
  parseHttpHeaderLine(formatHttpHeaderLine(original), parsed);
  EXPECT_EQ(parsed.name, original.name);
  EXPECT_EQ(parsed.value, original.value);
}

TEST(HttpHeaderValidity, AcceptsAccessTokensAndAnUnusedSlot) {
  EXPECT_TRUE(isValidHttpHeader("CF-Access-Client-Id", "abc123.access"));
  EXPECT_TRUE(isValidHttpHeader("CF-Access-Client-Secret", std::string(64, 'f')));
  EXPECT_TRUE(isValidHttpHeader("Authorization", "Bearer eyJ.a-b_c.d"));
  EXPECT_TRUE(isValidHttpHeader("X-Flag", ""));
  EXPECT_TRUE(isValidHttpHeader("", ""));
}

TEST(HttpHeaderValidity, RefusesAValueWithNoName) { EXPECT_FALSE(isValidHttpHeader("", "orphan")); }

TEST(HttpHeaderValidity, RefusesLineBreaksAndControlCharacters) {
  // Either one would end the header and start a forged one.
  EXPECT_FALSE(isValidHttpHeader("X-Key", "a\r\nX-Evil: 1"));
  EXPECT_FALSE(isValidHttpHeader("X-Key", "a\nb"));
  EXPECT_FALSE(isValidHttpHeader("X-Key", std::string("a\0b", 3)));
  EXPECT_FALSE(isValidHttpHeader("X-Key\r\nX-Evil", "1"));
  EXPECT_FALSE(isValidHttpHeader("X-Key", "caf\xC3\xA9"));
  EXPECT_TRUE(isValidHttpHeader("X-Key", "a\tb"));
}

TEST(HttpHeaderValidity, RefusesNamesThatAreNotTokens) {
  EXPECT_FALSE(isValidHttpHeader("X Key", "1"));
  EXPECT_FALSE(isValidHttpHeader("X:Key", "1"));
  EXPECT_FALSE(isValidHttpHeader("X-Key ", "1"));
  EXPECT_FALSE(isValidHttpHeader("Köy", "1"));
}

TEST(HttpHeaderValidity, RefusesTheHeadersTheClientWritesItself) {
  for (const char* name : {"Host", "host", "Content-Length", "Content-Type", "Transfer-Encoding", "Connection", "Range",
                           "User-Agent", "Upgrade", "TE", "Expect"}) {
    EXPECT_FALSE(isValidHttpHeader(name, "x")) << name;
  }
}

TEST(HttpHeaderValidity, EnforcesTheLengthLimits) {
  EXPECT_TRUE(isValidHttpHeader(std::string(HTTP_HEADER_NAME_MAX, 'a'), std::string(HTTP_HEADER_VALUE_MAX, 'v')));
  EXPECT_FALSE(isValidHttpHeader(std::string(HTTP_HEADER_NAME_MAX + 1, 'a'), "v"));
  EXPECT_FALSE(isValidHttpHeader("X-Key", std::string(HTTP_HEADER_VALUE_MAX + 1, 'v')));
}

TEST(HttpHeaderNames, CompareIgnoringCase) {
  EXPECT_TRUE(isHttpHeaderNamed("AUTHORIZATION", "authorization"));
  EXPECT_FALSE(isHttpHeaderNamed("Authorization-X", "authorization"));
}

namespace {
using Lines = std::vector<request_headers::Line>;
}

TEST(RequestHeadersTest, CustomHeadersGoFirstAndBasicFollows) {
  HttpRequestExtras extras;
  extras.headers = {{"CF-Access-Client-Id", "id"}, {"CF-Access-Client-Secret", "secret"}};
  const Lines expected = {
      {"CF-Access-Client-Id", "id"}, {"CF-Access-Client-Secret", "secret"}, {"Authorization", "Basic dTpw"}};
  EXPECT_EQ(request_headers::build(extras, "Basic dTpw"), expected);
}

TEST(RequestHeadersTest, BearerReplacesBasic) {
  HttpRequestExtras extras;
  extras.bearer = "tok";
  const Lines expected = {{"Authorization", "Bearer tok"}};
  EXPECT_EQ(request_headers::build(extras, "Basic dTpw"), expected);
}

TEST(RequestHeadersTest, ACustomAuthorizationReplacesBearerAndBasic) {
  HttpRequestExtras extras;
  extras.bearer = "tok";
  extras.headers = {{"authorization", "Bearer static"}};
  const Lines expected = {{"authorization", "Bearer static"}};
  EXPECT_EQ(request_headers::build(extras, "Basic dTpw"), expected);
}

TEST(RequestHeadersTest, InvalidHeadersAreLeftOut) {
  HttpRequestExtras extras;
  extras.headers = {{"X-Ok", "1"}, {"X-Split", "a\r\nX-Evil: 1"}, {"Host", "evil.example"}, {"", "orphan"}};
  const Lines expected = {{"X-Ok", "1"}};
  EXPECT_EQ(request_headers::build(extras, ""), expected);
}

TEST(RequestHeadersTest, NoCredentialsMeansNoHeaders) { EXPECT_TRUE(request_headers::build({}, "").empty()); }

TEST(HttpHeaderEdit, EmptyValueUnderTheSavedNameKeepsTheSavedValue) {
  const HttpHeader saved{"CF-Access-Client-Secret", "s3cret"};
  HttpHeader header;
  parseHttpHeaderEdit("cf-access-client-secret: ", saved, header);
  EXPECT_EQ(header.value, "s3cret");
  parseHttpHeaderEdit("CF-Access-Client-Secret: new", saved, header);
  EXPECT_EQ(header.value, "new");
  parseHttpHeaderEdit("Other-Name:", saved, header);  // a renamed header does not inherit the secret
  EXPECT_EQ(header.value, "");
  parseHttpHeaderEdit("", saved, header);  // an empty line clears the slot
  EXPECT_TRUE(header.name.empty());
  EXPECT_TRUE(header.value.empty());
}
