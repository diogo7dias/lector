#include <OpdsAuth.h>
#include <gtest/gtest.h>

#include <string>

using namespace opds_auth;

namespace {
bool parse(const std::string& json, AuthDocument& out) { return parseAuthDocument(json.data(), json.size(), out); }
}  // namespace

TEST(OpdsAuthDocument, FindsThePasswordFlowLinks) {
  AuthDocument doc;
  ASSERT_TRUE(parse(R"({"id":"x","title":"t","authentication":[
      {"type":"http://opds-spec.org/auth/basic"},
      {"type":"http://opds-spec.org/auth/oauth/password","links":[
        {"rel":"refresh","href":"/oauth/refresh"},
        {"rel":"authenticate","href":"https://books.example/oauth/token"}]}]})",
                    doc));
  EXPECT_EQ(doc.tokenUrl, "https://books.example/oauth/token");
  EXPECT_EQ(doc.refreshUrl, "/oauth/refresh");
}

TEST(OpdsAuthDocument, AcceptsRelAsAnArray) {
  AuthDocument doc;
  ASSERT_TRUE(parse(R"({"authentication":[{"type":"http://opds-spec.org/auth/oauth/password","links":[
      {"rel":["authenticate","other"],"href":"token"}]}]})",
                    doc));
  EXPECT_EQ(doc.tokenUrl, "token");
  EXPECT_TRUE(doc.refreshUrl.empty());
}

TEST(OpdsAuthDocument, KeysMayComeInAnyOrder) {
  AuthDocument doc;
  ASSERT_TRUE(parse(R"({"authentication":[{"links":[{"href":"token","rel":"authenticate"}],
      "type":"http://opds-spec.org/auth/oauth/password"}]})",
                    doc));
  EXPECT_EQ(doc.tokenUrl, "token");
}

TEST(OpdsAuthDocument, WhatTheDeviceCannotDriveIsNotAFlow) {
  AuthDocument doc;
  // Basic only: Basic is already sent with the first request, so there is nothing to sign in to.
  EXPECT_FALSE(parse(R"({"authentication":[{"type":"http://opds-spec.org/auth/basic"}]})", doc));
  // The implicit grant signs in through a login page, maybe on another host: not driven.
  EXPECT_FALSE(parse(R"({"authentication":[{"type":"http://opds-spec.org/auth/oauth/implicit","links":[
      {"rel":"authenticate","href":"https://sso.example/login"}]}]})",
                     doc));
  // A password flow with no authenticate link has nowhere to post.
  EXPECT_FALSE(parse(R"({"authentication":[{"type":"http://opds-spec.org/auth/oauth/password"}]})", doc));
  EXPECT_FALSE(parse("", doc));
  EXPECT_FALSE(parse("<html>401</html>", doc));
  EXPECT_FALSE(parse("{", doc));
}

TEST(OpdsTokenResponse, ReadsBothTokens) {
  TokenResponse token;
  const std::string json =
      R"({"access_token":"abc.def","token_type":"Bearer","expires_in":3600,"refresh_token":"r-1"})";
  ASSERT_TRUE(parseTokenResponse(json.data(), json.size(), token));
  EXPECT_EQ(token.accessToken, "abc.def");
  EXPECT_EQ(token.refreshToken, "r-1");
}

TEST(OpdsTokenResponse, ARefreshTokenIsOptional) {
  TokenResponse token;
  const std::string json = R"({"access_token":"abc"})";
  ASSERT_TRUE(parseTokenResponse(json.data(), json.size(), token));
  EXPECT_TRUE(token.refreshToken.empty());
}

TEST(OpdsTokenResponse, KeepsALongTokenWholeAndRefusesOneTooLongToStore) {
  TokenResponse token;
  const std::string longToken(1500, 'a');  // a JWT with claims, past a 512 byte parser buffer
  const std::string json = R"({"access_token":")" + longToken + R"("})";
  ASSERT_TRUE(parseTokenResponse(json.data(), json.size(), token));
  EXPECT_EQ(token.accessToken, longToken);

  const std::string huge = R"({"access_token":")" + std::string(MAX_TOKEN_CHARS + 1, 'a') + R"("})";
  EXPECT_FALSE(parseTokenResponse(huge.data(), huge.size(), token));
}

TEST(OpdsTokenResponse, RefusesAnAnswerWithoutAToken) {
  TokenResponse token;
  for (const char* json :
       {"", "{}", R"({"error":"invalid_grant"})", R"({"access_token":""})", R"({"access_token":7})", "not json"}) {
    EXPECT_FALSE(parseTokenResponse(json, std::string(json).size(), token)) << json;
  }
}

TEST(OpdsFormBodies, EncodeEverythingButUnreservedCharacters) {
  EXPECT_EQ(percentEncode("aZ09-._~"), "aZ09-._~");
  EXPECT_EQ(percentEncode("p&ss w=rd+é"), "p%26ss%20w%3Drd%2B%C3%A9");
  EXPECT_EQ(passwordGrantBody("me@books.example", "a&b=c"),
            "grant_type=password&username=me%40books.example&password=a%26b%3Dc");
  EXPECT_EQ(refreshGrantBody("r/1+2"), "grant_type=refresh_token&refresh_token=r%2F1%2B2");
}
