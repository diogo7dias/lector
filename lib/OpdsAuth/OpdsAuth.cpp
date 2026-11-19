#include "OpdsAuth.h"

#include <ArduinoJson.h>

#include <cstring>
#include <string_view>

namespace opds_auth {

namespace {

// "rel" is one string or an array of them.
bool hasRel(JsonVariantConst rel, const char* wanted) {
  if (rel.is<const char*>()) return std::strcmp(rel.as<const char*>(), wanted) == 0;
  for (JsonVariantConst one : rel.as<JsonArrayConst>()) {
    if ((one | "") == std::string_view(wanted)) return true;
  }
  return false;
}

}  // namespace

bool parseAuthDocument(const char* json, const size_t len, AuthDocument& out) {
  out = AuthDocument{};
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  for (JsonObjectConst flow : doc["authentication"].as<JsonArrayConst>()) {
    const char* type = flow["type"] | "";
    if (!std::strstr(type, "auth/oauth/password")) continue;
    for (JsonObjectConst link : flow["links"].as<JsonArrayConst>()) {
      const char* href = link["href"] | "";
      if (!*href) continue;
      if (out.tokenUrl.empty() && hasRel(link["rel"], "authenticate")) out.tokenUrl = href;
      if (out.refreshUrl.empty() && hasRel(link["rel"], "refresh")) out.refreshUrl = href;
    }
    return !out.tokenUrl.empty();
  }
  return false;
}

bool parseTokenResponse(const char* json, const size_t len, TokenResponse& out) {
  out = TokenResponse{};
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  const char* access = doc["access_token"] | "";
  const char* refresh = doc["refresh_token"] | "";
  if (!*access || std::strlen(access) > MAX_TOKEN_CHARS || std::strlen(refresh) > MAX_TOKEN_CHARS) return false;
  out.accessToken = access;
  out.refreshToken = refresh;
  return true;
}

std::string percentEncode(const std::string& value) {
  static const char digits[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '-' ||
        byte == '.' || byte == '_' || byte == '~') {
      out += c;
    } else {
      out += '%';
      out += digits[byte >> 4];
      out += digits[byte & 15];
    }
  }
  return out;
}

std::string passwordGrantBody(const std::string& username, const std::string& password) {
  return "grant_type=password&username=" + percentEncode(username) + "&password=" + percentEncode(password);
}

std::string refreshGrantBody(const std::string& refreshToken) {
  return "grant_type=refresh_token&refresh_token=" + percentEncode(refreshToken);
}

}  // namespace opds_auth
