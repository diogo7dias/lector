#include "OpdsTokenStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>
#include <OpdsAuth.h>

#include <algorithm>

void OpdsTokenStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["tokens"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["key"] = entry.key;
    obj["access_obf"] = obfuscation::obfuscateToBase64(entry.tokens.accessToken);
    obj["refresh_obf"] = obfuscation::obfuscateToBase64(entry.tokens.refreshToken);
    obj["refresh_url"] = entry.tokens.refreshUrl;
  }
}

bool OpdsTokenStore::fromJson(JsonVariantConst doc) {
  entries.clear();
  JsonArrayConst arr = doc["tokens"].as<JsonArrayConst>();
  entries.reserve(std::min(arr.size(), MAX_SERVERS));
  for (JsonObjectConst obj : arr) {
    if (entries.size() >= MAX_SERVERS) break;
    Entry entry;
    entry.key = obj["key"] | "";
    bool ok = false;
    bool tooLong = false;
    entry.tokens.accessToken =
        obfuscation::deobfuscateFromBase64(obj["access_obf"] | "", opds_auth::MAX_TOKEN_CHARS, &ok, &tooLong);
    // A token that did not decode (another chip's key, a damaged file) is no token at all.
    if (entry.key.empty() || !ok) continue;
    entry.tokens.refreshToken =
        obfuscation::deobfuscateFromBase64(obj["refresh_obf"] | "", opds_auth::MAX_TOKEN_CHARS, &ok, &tooLong);
    if (ok) entry.tokens.refreshUrl = obj["refresh_url"] | "";
    entries.push_back(std::move(entry));
  }
  LOG_DBG("OPDSTOK", "Loaded %zu OPDS token entries", entries.size());
  return true;
}

OpdsTokens OpdsTokenStore::get(const std::string& key) const {
  for (const auto& entry : entries) {
    if (entry.key == key) return entry.tokens;
  }
  return {};
}

bool OpdsTokenStore::put(const std::string& key, const OpdsTokens& tokens) {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.key == key; });
  const bool storable = tokens.accessToken.size() <= opds_auth::MAX_TOKEN_CHARS &&
                        tokens.refreshToken.size() <= opds_auth::MAX_TOKEN_CHARS;

  if (tokens.empty() || !storable) {
    // Clearing: drop the entry if present; nothing to write otherwise.
    if (it != entries.end()) {
      entries.erase(it);
      if (!saveToFile()) return false;
    }
    return tokens.empty();
  }

  if (it != entries.end()) {
    if (it->tokens.accessToken == tokens.accessToken && it->tokens.refreshToken == tokens.refreshToken &&
        it->tokens.refreshUrl == tokens.refreshUrl) {
      return true;  // unchanged: skip the SD write
    }
    it->tokens = tokens;
    return saveToFile();
  }

  // Full: the oldest entry makes room, so a new server can still be remembered.
  if (entries.size() >= MAX_SERVERS) entries.erase(entries.begin());
  entries.push_back(Entry{key, tokens});
  return saveToFile();
}
