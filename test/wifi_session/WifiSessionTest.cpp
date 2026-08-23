#include <gtest/gtest.h>

#include <vector>

#include "WifiSession/WifiSession.h"

namespace {

using wifi_session::Network;
using wifi_session::WifiSession;

Network seen(const char* ssid, int32_t rssi, bool encrypted = true) {
  Network network;
  snprintf(network.ssid, sizeof(network.ssid), "%s", ssid);
  network.rssi = rssi;
  network.isEncrypted = encrypted;
  return network;
}

TEST(WifiSessionScan, KeepsTheStrongestSightingOfARepeatedSsid) {
  WifiSession session;
  session.begin({}, 1000);

  const std::vector<Network> found = {seen("mesh", -80), seen("mesh", -40), seen("mesh", -65)};
  session.onScanResults(found.data(), found.size(), 2000);

  ASSERT_EQ(session.networks().size(), 1u);
  EXPECT_STREQ(session.networks()[0].ssid, "mesh");
  EXPECT_EQ(session.networks()[0].rssi, -40);
}

TEST(WifiSessionScan, SortsSavedNetworksAheadOfStrongerUnsavedOnes) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home"};
  session.begin(startup, 1000);

  const std::vector<Network> found = {seen("cafe", -30), seen("home", -70)};
  session.onScanResults(found.data(), found.size(), 2000);

  ASSERT_EQ(session.networks().size(), 2u);
  EXPECT_STREQ(session.networks()[0].ssid, "home");
  EXPECT_TRUE(session.networks()[0].hasSavedPassword);
  EXPECT_STREQ(session.networks()[1].ssid, "cafe");
  EXPECT_FALSE(session.networks()[1].hasSavedPassword);
}

std::vector<wifi_session::Action> drain(WifiSession& session, uint32_t nowMs) {
  std::vector<wifi_session::Action> actions;
  wifi_session::Action action;
  while (session.nextAction(nowMs, action)) {
    actions.push_back(action);
  }
  return actions;
}

TEST(WifiSessionStart, JoinsTheLastConnectedNetworkBeforeScanning) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home", "cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);

  const std::vector<wifi_session::Action> actions = drain(session, 1000);

  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::JOIN);
  EXPECT_EQ(actions[0].ssid, "cafe");
  EXPECT_TRUE(actions[0].useSavedPassword);
  EXPECT_EQ(session.state(), wifi_session::State::AUTO_CONNECTING);
}

TEST(WifiSessionAutoConnect, ScansAfterTheLastConnectedNetworkFails) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home", "cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  drain(session, 1000);

  session.onJoinFailed(2000);

  const std::vector<wifi_session::Action> actions = drain(session, 2000);
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::DISCONNECT);
  EXPECT_EQ(actions[1].kind, wifi_session::ActionKind::START_SCAN);
  EXPECT_EQ(session.state(), wifi_session::State::AUTO_CONNECTING);
}

TEST(WifiSessionAutoConnect, TriesEachSavedNetworkAtMostOnce) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home", "cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  drain(session, 1000);
  session.onJoinFailed(2000);
  drain(session, 2000);

  const std::vector<Network> found = {seen("home", -50), seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 3000);

  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::JOIN);
  EXPECT_EQ(actions[0].ssid, "home");
}

TEST(WifiSessionAutoConnect, ShowsTheListOnceEverySavedNetworkHasFailed) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  drain(session, 1000);
  session.onJoinFailed(2000);
  drain(session, 2000);

  const std::vector<Network> found = {seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 3000);

  EXPECT_TRUE(drain(session, 3000).empty());
  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
}

TEST(WifiSessionScan, ShowsTheListOnceResultsArrive) {
  WifiSession session;
  session.begin({}, 1000);
  drain(session, 1000);

  const std::vector<Network> found = {seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 2000);

  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
}

TEST(WifiSessionScan, GivesUpOnAScanThatNeverCompletes) {
  WifiSession session;
  session.begin({}, 1000);
  ASSERT_EQ(drain(session, 1000).size(), 1u);

  EXPECT_TRUE(drain(session, 1000 + wifi_session::SCAN_TIMEOUT_MS - 1).empty());
  EXPECT_EQ(session.state(), wifi_session::State::SCANNING);

  EXPECT_TRUE(drain(session, 1000 + wifi_session::SCAN_TIMEOUT_MS).empty());
  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
  EXPECT_TRUE(session.networks().empty());
}

TEST(WifiSessionJoin, GivesUpOnAManualJoinAfterTheJoinTimeout) {
  WifiSession session;
  session.begin({}, 1000);
  drain(session, 1000);
  const std::vector<Network> found = {seen("cafe", -40, false)};
  session.onScanResults(found.data(), found.size(), 2000);
  session.selectNetwork(0, 2000);
  ASSERT_EQ(drain(session, 2000).size(), 1u);

  EXPECT_TRUE(drain(session, 2000 + wifi_session::JOIN_TIMEOUT_MS - 1).empty());
  EXPECT_EQ(session.state(), wifi_session::State::CONNECTING);

  const std::vector<wifi_session::Action> actions = drain(session, 2000 + wifi_session::JOIN_TIMEOUT_MS);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::DISCONNECT);
  EXPECT_EQ(session.state(), wifi_session::State::FAILED);
}

TEST(WifiSessionJoin, GivesUpOnAnAutomaticJoinSooner) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  ASSERT_EQ(drain(session, 1000).size(), 1u);

  EXPECT_TRUE(drain(session, 1000 + wifi_session::AUTO_JOIN_TIMEOUT_MS - 1).empty());

  // The automatic path moves on to the next candidate instead of stopping.
  const std::vector<wifi_session::Action> actions = drain(session, 1000 + wifi_session::AUTO_JOIN_TIMEOUT_MS);
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::DISCONNECT);
  EXPECT_EQ(actions[1].kind, wifi_session::ActionKind::START_SCAN);
}

WifiSession atNetworkList(const std::vector<Network>& found) {
  WifiSession session;
  session.begin({}, 1000);
  drain(session, 1000);
  session.onScanResults(found.data(), found.size(), 2000);
  drain(session, 2000);
  return session;
}

TEST(WifiSessionJoin, AsksForAPasswordBeforeJoiningAnEncryptedStranger) {
  WifiSession session = atNetworkList({seen("cafe", -40)});

  session.selectNetwork(0, 3000);

  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::ASK_FOR_PASSWORD);
  EXPECT_EQ(actions[0].ssid, "cafe");
}

TEST(WifiSessionJoin, JoinsWithTheTypedPasswordOnceItArrives) {
  WifiSession session = atNetworkList({seen("cafe", -40)});
  session.selectNetwork(0, 3000);
  drain(session, 3000);

  session.onPasswordEntered(4000);

  const std::vector<wifi_session::Action> actions = drain(session, 4000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::JOIN);
  EXPECT_EQ(actions[0].ssid, "cafe");
  EXPECT_FALSE(actions[0].useSavedPassword);
}

TEST(WifiSessionJoin, OffersToRememberAPasswordTheReaderTyped) {
  WifiSession session = atNetworkList({seen("cafe", -40)});
  session.selectNetwork(0, 3000);
  drain(session, 3000);
  session.onPasswordEntered(4000);
  drain(session, 4000);

  session.onJoinSucceeded(5000);

  EXPECT_EQ(session.state(), wifi_session::State::CONNECTED);
  EXPECT_TRUE(session.offersToSaveCredential());
  EXPECT_TRUE(drain(session, 5000).empty());

  session.answerSavePrompt(true, 6000);

  const std::vector<wifi_session::Action> actions = drain(session, 6000);
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::SAVE_CREDENTIAL);
  EXPECT_EQ(actions[0].ssid, "cafe");
  EXPECT_EQ(actions[1].kind, wifi_session::ActionKind::FINISH);
  EXPECT_TRUE(actions[1].connected);
}

TEST(WifiSessionJoin, ForgetsAnAbandonedPasswordEntry) {
  WifiSession session = atNetworkList({seen("cafe", -40), seen("open-guest", -60, false)});
  session.selectNetwork(0, 3000);
  drain(session, 3000);

  session.abandonPasswordEntry(4000);
  session.selectNetwork(1, 5000);
  drain(session, 5000);
  session.onJoinSucceeded(6000);

  EXPECT_FALSE(session.offersToSaveCredential());
}

TEST(WifiSessionJoin, DoesNotOfferToRememberAPasswordItAlreadyHad) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  drain(session, 1000);

  session.onJoinSucceeded(2000);

  EXPECT_FALSE(session.offersToSaveCredential());
  const std::vector<wifi_session::Action> actions = drain(session, 2000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::FINISH);
  EXPECT_TRUE(actions[0].connected);
}

TEST(WifiSessionScan, GivesUpOnAnAutomaticScanThatNeverCompletes) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home"};
  session.begin(startup, 1000);
  ASSERT_EQ(drain(session, 1000).size(), 1u);
  ASSERT_EQ(session.state(), wifi_session::State::AUTO_CONNECTING);

  EXPECT_TRUE(drain(session, 1000 + wifi_session::SCAN_TIMEOUT_MS).empty());
  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
}

TEST(WifiSessionScan, ShowsAnEmptyListWhenTheScanFails) {
  WifiSession session;
  session.begin({}, 1000);
  drain(session, 1000);

  session.onScanFailed(2000);

  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
  EXPECT_TRUE(session.networks().empty());
  EXPECT_TRUE(drain(session, 2000).empty());
}

TEST(WifiSessionAutoConnect, StopsTryingSavedNetworksOnceTheReaderAsksForTheList) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.savedSsids = {"home", "cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);
  drain(session, 1000);

  session.showNetworkList(2000);

  const std::vector<wifi_session::Action> actions = drain(session, 2000);
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::DISCONNECT);
  EXPECT_EQ(actions[1].kind, wifi_session::ActionKind::START_SCAN);
  EXPECT_EQ(session.state(), wifi_session::State::SCANNING);

  const std::vector<Network> found = {seen("home", -50)};
  session.onScanResults(found.data(), found.size(), 3000);

  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
  EXPECT_TRUE(drain(session, 3000).empty());
}

TEST(WifiSessionForget, ForgettingACredentialRescans) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"cafe"};
  session.begin(startup, 1000);
  drain(session, 1000);
  const std::vector<Network> found = {seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 2000);
  drain(session, 2000);

  session.forgetNetwork(0, 3000);

  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 2u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::FORGET_CREDENTIAL);
  EXPECT_EQ(actions[0].ssid, "cafe");
  EXPECT_EQ(actions[1].kind, wifi_session::ActionKind::START_SCAN);
  EXPECT_EQ(session.state(), wifi_session::State::SCANNING);
}

TEST(WifiSessionForget, AFailedJoinNeverOffersToForgetTheCredential) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"cafe"};
  session.begin(startup, 1000);
  drain(session, 1000);
  const std::vector<Network> found = {seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 2000);
  drain(session, 2000);
  session.selectNetwork(0, 3000);
  drain(session, 3000);

  session.onJoinFailed(4000);

  EXPECT_EQ(session.state(), wifi_session::State::FAILED);
  for (const wifi_session::Action& action : drain(session, 4000)) {
    EXPECT_NE(action.kind, wifi_session::ActionKind::FORGET_CREDENTIAL);
  }

  // A passing failure sends the reader back to the list with the credential intact.
  session.dismissFailure(5000);
  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
  ASSERT_EQ(session.networks().size(), 1u);
  EXPECT_TRUE(session.networks()[0].hasSavedPassword);
}

TEST(WifiSessionCancel, LeavingWithoutAConnectionFinishesUnconnected) {
  WifiSession session = atNetworkList({seen("cafe", -40)});

  session.cancel(3000);

  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::FINISH);
  EXPECT_FALSE(actions[0].connected);
}

TEST(WifiSessionHidden, JoinsAHiddenNetworkTheReaderNamed) {
  WifiSession session = atNetworkList({seen("cafe", -40)});

  session.selectHiddenNetwork("backroom", 3000);

  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::ASK_FOR_PASSWORD);
  EXPECT_EQ(actions[0].ssid, "backroom");
}

TEST(WifiSessionHidden, UsesTheStoredPasswordForAKnownHiddenNetwork) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"backroom"};
  session.begin(startup, 1000);
  drain(session, 1000);

  session.selectHiddenNetwork("backroom", 2000);

  const std::vector<wifi_session::Action> actions = drain(session, 2000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::JOIN);
  EXPECT_TRUE(actions[0].useSavedPassword);
}

}  // namespace
