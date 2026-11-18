#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

/**
 * OAuth token state for one OPDS server (the password grant, see lib/OpdsAuth).
 * The access and refresh tokens are bearer credentials: stored obfuscated with the device
 * key like the server password, never logged. refreshUrl is the token endpoint that
 * renews an expired access token without asking the server for a full login again.
 */
struct OpdsTokens {
  std::string accessToken;
  std::string refreshToken;
  std::string refreshUrl;

  bool empty() const { return accessToken.empty(); }
};

/**
 * Persists per-server tokens on the card (/.crosspoint/opds_tokens.json), so the device
 * does not sign in on every visit. A server is keyed by its URL and username: a different
 * user on the same catalogue never inherits another one's token.
 */
class OpdsTokenStore : public PersistableStore<OpdsTokenStore> {
 private:
  struct Entry {
    std::string key;
    OpdsTokens tokens;
  };
  std::vector<Entry> entries;
  static constexpr size_t MAX_SERVERS = 8;

  OpdsTokenStore() = default;
  friend class PersistableStore<OpdsTokenStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/opds_tokens.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static std::string keyFor(const std::string& serverUrl, const std::string& username) {
    return serverUrl + '\n' + username;
  }

  // The stored tokens for a key (empty when none).
  OpdsTokens get(const std::string& key) const;
  // Upserts the tokens for a key. Writes the card only when something changed; empty
  // tokens remove the entry. Tokens too long to read back are refused, and a refused
  // update drops the old entry too: better no token than a stale one.
  bool put(const std::string& key, const OpdsTokens& tokens);
};

// Read on first use: only the OPDS screens touch it, and every deep-sleep wake is a boot.
#define OPDS_TOKENS OpdsTokenStore::lazyInstance()
