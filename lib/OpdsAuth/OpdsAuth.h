#pragma once

#include <cstddef>
#include <string>

// The pure half of OPDS sign-in (Authentication for OPDS 1.0): reading the server's
// authentication document and its token answers, and building the form bodies. The
// network half lives in opds::OpdsClient.
//
// Only the OAuth password grant is driven: the device posts the stored username and
// password to the token endpoint and keeps the bearer token it gets back. The implicit
// grant is not: it signs in through an HTML login form on a page the server names, which
// can be a single sign-on on another host, and the stored password only goes to the
// catalogue's own host.
namespace opds_auth {

// Longest token the store keeps, and the longest a token answer may carry.
constexpr size_t MAX_TOKEN_CHARS = 2048;

struct AuthDocument {
  // "authenticate" and "refresh" links of the password flow, as written (maybe relative).
  std::string tokenUrl;
  std::string refreshUrl;
};

// True when the document offers the OAuth password flow with an authenticate link.
// A Basic-only document, a plain 401 body or malformed JSON is false.
bool parseAuthDocument(const char* json, size_t len, AuthDocument& out);

struct TokenResponse {
  std::string accessToken;
  std::string refreshToken;  // empty when the server issued none
};

// True when the answer carries an access_token. A token over MAX_TOKEN_CHARS is refused
// rather than cut: a cut token is a different token.
bool parseTokenResponse(const char* json, size_t len, TokenResponse& out);

// Percent-encodes everything but unreserved characters, for a form value.
std::string percentEncode(const std::string& value);

std::string passwordGrantBody(const std::string& username, const std::string& password);
std::string refreshGrantBody(const std::string& refreshToken);

}  // namespace opds_auth
