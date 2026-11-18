#include <ArduinoJson.h>
#include <OpdsAuth.h>
#include <gtest/gtest.h>

#include <string>

#include "OpdsTokenStore.h"

namespace {

OpdsTokenStore& store() { return OpdsTokenStore::getInstance(); }

std::string dump() {
  JsonDocument doc;
  store().toJson(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// Saves and reloads, as a reboot would.
void reload() {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, dump()));
  ASSERT_TRUE(store().fromJson(doc.as<JsonVariantConst>()));
}

OpdsTokens tokens(const std::string& access, const std::string& refresh = "", const std::string& url = "") {
  return OpdsTokens{access, refresh, url};
}

}  // namespace

TEST(OpdsTokenStore, SurvivesAReboot) {
  const std::string key = OpdsTokenStore::keyFor("https://books.example/opds", "reader");
  ASSERT_TRUE(store().put(key, tokens("access-1", "refresh-1", "https://books.example/oauth/token")));
  reload();
  const OpdsTokens back = store().get(key);
  EXPECT_EQ(back.accessToken, "access-1");
  EXPECT_EQ(back.refreshToken, "refresh-1");
  EXPECT_EQ(back.refreshUrl, "https://books.example/oauth/token");
}

TEST(OpdsTokenStore, TokensAreNeverWrittenInTheClear) {
  ASSERT_TRUE(store().put("k", tokens("topsecretaccess", "topsecretrefresh")));
  const std::string saved = dump();
  EXPECT_EQ(saved.find("topsecretaccess"), std::string::npos);
  EXPECT_EQ(saved.find("topsecretrefresh"), std::string::npos);
}

TEST(OpdsTokenStore, AnotherUserOnTheSameCatalogueGetsNoToken) {
  ASSERT_TRUE(store().put(OpdsTokenStore::keyFor("https://b.example/opds", "ann"), tokens("ann-token")));
  EXPECT_TRUE(store().get(OpdsTokenStore::keyFor("https://b.example/opds", "bob")).empty());
  EXPECT_EQ(store().get(OpdsTokenStore::keyFor("https://b.example/opds", "ann")).accessToken, "ann-token");
}

TEST(OpdsTokenStore, EmptyTokensRemoveTheEntry) {
  ASSERT_TRUE(store().put("gone", tokens("a")));
  ASSERT_TRUE(store().put("gone", {}));
  EXPECT_TRUE(store().get("gone").empty());
  reload();
  EXPECT_TRUE(store().get("gone").empty());
}

TEST(OpdsTokenStore, ATokenTooLongToReadBackIsRefusedAndDropsTheOldOne) {
  ASSERT_TRUE(store().put("long", tokens("old")));
  EXPECT_FALSE(store().put("long", tokens(std::string(opds_auth::MAX_TOKEN_CHARS + 1, 'x'))));
  EXPECT_TRUE(store().get("long").empty());
}

TEST(OpdsTokenStore, AFullStoreForgetsTheOldestServerFirst) {
  for (int i = 0; i < 9; i++) ASSERT_TRUE(store().put("full-" + std::to_string(i), tokens("t" + std::to_string(i))));
  EXPECT_TRUE(store().get("full-0").empty());
  EXPECT_EQ(store().get("full-8").accessToken, "t8");
}

TEST(OpdsTokenStore, AnEntryThatDoesNotDecodeIsDropped) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"tokens":[{"key":"bad","access_obf":"zz","refresh_obf":"","refresh_url":""},
                                       {"key":"","access_obf":"00"}]})"));
  ASSERT_TRUE(store().fromJson(doc.as<JsonVariantConst>()));
  EXPECT_TRUE(store().get("bad").empty());
}

TEST(OpdsTokenStore, AnEmptyFileIsJustEmpty) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, "{}"));
  ASSERT_TRUE(store().fromJson(doc.as<JsonVariantConst>()));
  EXPECT_TRUE(store().get("anything").empty());
}
