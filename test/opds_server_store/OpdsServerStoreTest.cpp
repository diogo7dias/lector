#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <string>

#include "ObfuscationUtils.h"
#include "OpdsServerStore.h"

namespace {

OpdsServerStore& store() { return OpdsServerStore::getInstance(); }

// Loads `json` into the store as if it had been read from the card.
void load(const std::string& json) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, json));
  ASSERT_TRUE(store().fromJson(doc.as<JsonVariantConst>()));
}

std::string dump() {
  JsonDocument doc;
  store().toJson(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string entry(const std::string& headers) {
  return R"({"servers":[{"name":"n","url":"https://books.example/opds","username":"u","password_obf":")" +
         obfuscation::obfuscateToBase64("pw") + "\"" + headers + "}]}";
}

std::string header(const std::string& name, const std::string& value) {
  return R"({"name":")" + name + R"(","value_obf":")" + obfuscation::obfuscateToBase64(value) + "\"}";
}

}  // namespace

TEST(OpdsServerStoreHeaders, SurviveASaveAndALoad) {
  load(entry(""));
  OpdsServer server = *store().getServer(0);
  server.customHeaders[0] = {"CF-Access-Client-Id", "id-123.access"};
  server.customHeaders[1] = {"CF-Access-Client-Secret", "s3cret-value"};
  ASSERT_TRUE(store().updateServer(0, server));

  const std::string saved = dump();
  load(saved);

  const OpdsServer* back = store().getServer(0);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->customHeaders[0].name, "CF-Access-Client-Id");
  EXPECT_EQ(back->customHeaders[0].value, "id-123.access");
  EXPECT_EQ(back->customHeaders[1].name, "CF-Access-Client-Secret");
  EXPECT_EQ(back->customHeaders[1].value, "s3cret-value");
  EXPECT_EQ(back->password, "pw");
}

TEST(OpdsServerStoreHeaders, ValuesAreNeverWrittenInTheClear) {
  load(entry(R"(,"headers":[)" + header("X-Token", "tokenvalue") + "]"));
  const std::string saved = dump();
  EXPECT_NE(saved.find("X-Token"), std::string::npos);
  EXPECT_EQ(saved.find("tokenvalue"), std::string::npos);
  EXPECT_EQ(saved.find("\"pw\""), std::string::npos);
}

TEST(OpdsServerStoreHeaders, AFileFromBeforeHeadersLoadsWithNoneAndIsWrittenBackWithoutAKey) {
  load(entry(""));
  const OpdsServer* server = store().getServer(0);
  ASSERT_NE(server, nullptr);
  EXPECT_TRUE(server->customHeaders[0].name.empty());
  EXPECT_TRUE(server->activeCustomHeaders().empty());
  EXPECT_EQ(dump().find("headers"), std::string::npos);
}

TEST(OpdsServerStoreHeaders, ADamagedOrForgedEntryIsDroppedWhole) {
  load(entry(R"(,"headers":[)" + header("Host", "evil.example") + "," + header("X-Ok", "fine") + "]"));
  EXPECT_TRUE(store().getServer(0)->customHeaders[0].name.empty());
  EXPECT_TRUE(store().getServer(0)->customHeaders[0].value.empty());
  EXPECT_EQ(store().getServer(0)->customHeaders[1].name, "X-Ok");

  // A value that does not decode (another chip's key, or a hand edit) leaves a name with
  // no value; the slot is only kept when that is a valid header.
  load(entry(R"(,"headers":[{"name":"X-Key","value_obf":"zz"}])"));
  EXPECT_EQ(store().getServer(0)->customHeaders[0].name, "X-Key");
  EXPECT_TRUE(store().getServer(0)->customHeaders[0].value.empty());

  load(entry(R"(,"headers":[{"name":"X Key","value_obf":""}])"));
  EXPECT_TRUE(store().getServer(0)->customHeaders[0].name.empty());
}

TEST(OpdsServerStoreHeaders, OnlyTwoSlotsAreRead) {
  load(entry(R"(,"headers":[)" + header("A", "1") + "," + header("B", "2") + "," + header("C", "3") + "]"));
  const OpdsServer* server = store().getServer(0);
  EXPECT_EQ(server->customHeaders[0].name, "A");
  EXPECT_EQ(server->customHeaders[1].name, "B");
}

TEST(OpdsServerStoreHeaders, ActiveHeadersSkipUnusedSlots) {
  OpdsServer server;
  server.customHeaders[1] = {"X-Only", "v"};
  const auto active = server.activeCustomHeaders();
  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(active[0].first, "X-Only");
  EXPECT_EQ(active[0].second, "v");
}
