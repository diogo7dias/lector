#include <gtest/gtest.h>

#include <string>

#include "CredentialBundle.h"

namespace {

using namespace credential_bundle;

Bundle sample() {
  Bundle bundle;
  bundle.wifi.push_back({"Home \"Net\"", "p\\ss word"});
  bundle.wifi.push_back({"Phone", ""});
  bundle.opds.push_back({"Kumedia Library", "https://example.org:8450/opds", "reader", "secret"});
  return bundle;
}

TEST(CredentialBundle, SurvivesARoundTrip) {
  const Bundle original = sample();
  Bundle parsed;
  ASSERT_TRUE(parse(serialize(original), parsed));

  ASSERT_EQ(parsed.wifi.size(), 2u);
  EXPECT_EQ(parsed.wifi[0].ssid, "Home \"Net\"");
  EXPECT_EQ(parsed.wifi[0].password, "p\\ss word");
  EXPECT_EQ(parsed.wifi[1].ssid, "Phone");
  EXPECT_TRUE(parsed.wifi[1].password.empty());

  ASSERT_EQ(parsed.opds.size(), 1u);
  EXPECT_EQ(parsed.opds[0].name, "Kumedia Library");
  EXPECT_EQ(parsed.opds[0].url, "https://example.org:8450/opds");
  EXPECT_EQ(parsed.opds[0].username, "reader");
  EXPECT_EQ(parsed.opds[0].password, "secret");
}

TEST(CredentialBundle, RefusesAVersionItDoesNotKnow) {
  Bundle parsed;
  EXPECT_FALSE(parse(R"({"version": 99, "wifi": [], "opds": []})", parsed));
}

TEST(CredentialBundle, RefusesADocumentWithNoVersion) {
  Bundle parsed;
  EXPECT_FALSE(parse(R"({"wifi": [], "opds": []})", parsed));
}

TEST(CredentialBundle, RefusesMalformedJson) {
  Bundle parsed;
  EXPECT_FALSE(parse(R"({"version": 1, "wifi": [{"ssid": "x")", parsed));
  EXPECT_FALSE(parse("", parsed));
}

TEST(CredentialBundle, DropsEntriesMissingTheirIdentity) {
  // A network with no SSID and a server with no URL cannot be acted on, so they
  // are dropped rather than turning the whole bundle into a failure.
  Bundle parsed;
  ASSERT_TRUE(parse(R"({"version": 1,
    "wifi": [{"password": "x"}, {"ssid": "Good", "password": "y"}],
    "opds": [{"name": "No URL"}, {"name": "Fine", "url": "https://e/opds"}]})",
                    parsed));
  ASSERT_EQ(parsed.wifi.size(), 1u);
  EXPECT_EQ(parsed.wifi[0].ssid, "Good");
  ASSERT_EQ(parsed.opds.size(), 1u);
  EXPECT_EQ(parsed.opds[0].url, "https://e/opds");
}

TEST(CredentialBundle, IgnoresFieldsItDoesNotKnow) {
  Bundle parsed;
  ASSERT_TRUE(parse(R"({"version": 1, "extra": {"a": [1, 2]}, "wifi": [{"ssid": "S", "future": 3}], "opds": []})",
                    parsed));
  ASSERT_EQ(parsed.wifi.size(), 1u);
  EXPECT_EQ(parsed.wifi[0].ssid, "S");
}

TEST(CredentialBundle, StopsAtTheEntryCap) {
  std::string json = R"({"version": 1, "opds": [], "wifi": [)";
  for (size_t i = 0; i < MAX_ENTRIES + 5; i++) {
    if (i) json += ",";
    json += R"({"ssid": "N)" + std::to_string(i) + R"("})";
  }
  json += "]}";

  Bundle parsed;
  ASSERT_TRUE(parse(json, parsed));
  EXPECT_EQ(parsed.wifi.size(), MAX_ENTRIES);
}

TEST(CredentialBundle, RecognisesItsOwnFilename) {
  EXPECT_TRUE(isBundleFilename("credentials.cpcred"));
  EXPECT_TRUE(isBundleFilename("Credentials.CPCRED"));
  EXPECT_FALSE(isBundleFilename(".cpcred"));
  EXPECT_FALSE(isBundleFilename("book.epub"));
  EXPECT_FALSE(isBundleFilename("cpcred"));
}

}  // namespace
