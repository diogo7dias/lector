#include <DiagLog.h>
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

TEST(WifiSessionAutoConnect, SilentJoinsAndRepeatedScanResultsCannotRetryForever) {
  // Exercise the complete timeout -> disconnect -> scan -> next join cycle,
  // including millis() rollover. No radio failure event is needed to finish.
  for (const uint32_t start : {1000u, UINT32_MAX - 1000u}) {
    WifiSession session;
    wifi_session::Startup startup;
    startup.savedSsids = {"home", "cafe"};
    startup.lastConnectedSsid = "home";
    session.begin(startup, start);
    ASSERT_EQ(drain(session, start).size(), 1u);

    const std::vector<Network> found = {seen("home", -40), seen("cafe", -50)};
    uint32_t now = start;
    for (int attempt = 0; attempt < 2; ++attempt) {
      now += wifi_session::AUTO_JOIN_TIMEOUT_MS;
      const auto timedOut = drain(session, now);
      ASSERT_EQ(timedOut.size(), 2u);
      EXPECT_EQ(timedOut[0].kind, wifi_session::ActionKind::DISCONNECT);
      EXPECT_EQ(timedOut[1].kind, wifi_session::ActionKind::START_SCAN);
      session.onScanResults(found.data(), found.size(), ++now);
      const auto next = drain(session, now);
      if (attempt == 0) {
        ASSERT_EQ(next.size(), 1u);
        EXPECT_EQ(next[0].kind, wifi_session::ActionKind::JOIN);
        EXPECT_EQ(next[0].ssid, "cafe");
      } else {
        EXPECT_TRUE(next.empty());
      }
    }
    EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
    EXPECT_TRUE(drain(session, now + wifi_session::SCAN_TIMEOUT_MS).empty());
  }
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

// A typed password that failed must not ride along to the next join: answering
// "remember" after joining a saved network would overwrite its stored password
// with the (empty) typed one.
TEST(WifiSessionJoin, AFailedTypedPasswordIsNotOfferedAfterJoiningASavedNetwork) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"home"};
  session.begin(startup, 1000);
  drain(session, 1000);
  const std::vector<Network> found = {seen("home", -50), seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 2000);
  drain(session, 2000);
  ASSERT_STREQ(session.networks()[1].ssid, "cafe");

  session.selectNetwork(1, 3000);
  drain(session, 3000);
  session.onPasswordEntered(4000);
  drain(session, 4000);
  session.onJoinFailed(5000);
  session.dismissFailure(6000);

  session.selectNetwork(0, 7000);
  const std::vector<wifi_session::Action> join = drain(session, 7000);
  ASSERT_EQ(join.size(), 1u);
  EXPECT_TRUE(join[0].useSavedPassword);
  session.onJoinSucceeded(8000);

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

TEST(WifiSessionStart, DoesNotAutoJoinWhenAutoConnectDisabled) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"home", "cafe"};
  startup.lastConnectedSsid = "cafe";
  session.begin(startup, 1000);

  const std::vector<wifi_session::Action> actions = drain(session, 1000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::START_SCAN);
  EXPECT_EQ(session.state(), wifi_session::State::SCANNING);
}

TEST(WifiSessionScan, DoesNotAutoJoinFromScanWhenAutoConnectDisabled) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = {"home", "cafe"};
  session.begin(startup, 1000);
  drain(session, 1000);

  const std::vector<Network> found = {seen("home", -50), seen("cafe", -40)};
  session.onScanResults(found.data(), found.size(), 2000);

  EXPECT_TRUE(drain(session, 2000).empty());
  EXPECT_EQ(session.state(), wifi_session::State::NETWORK_LIST);
  ASSERT_EQ(session.networks().size(), 2u);

  // The reader can choose the second saved network instead of being forced into the first
  session.selectNetwork(1, 3000);
  const std::vector<wifi_session::Action> actions = drain(session, 3000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, wifi_session::ActionKind::JOIN);
  EXPECT_EQ(actions[0].ssid, "home");
  EXPECT_TRUE(actions[0].useSavedPassword);
}

// Exercise the file-producing diagnostics, including a clock wrap and a stopped
// drain. No radio or secret is needed to distinguish an overdue timer from one
// actually evaluated and fired.
TEST(WifiSessionDiagnostics, UndrainedAutomaticJoinThenTimeoutAndExhaustion) {
  for (const uint32_t start : {1000u, UINT32_MAX - 1000u}) {
    diaglog::clear();
    WifiSession session;
    wifi_session::Startup startup;
    startup.savedSsids = {"PRIVATE_NETWORK"};
    startup.lastConnectedSsid = "PRIVATE_NETWORK";
    session.begin(startup, start);
    drain(session, start);
    std::string trace(diaglog::data(), diaglog::size());
    EXPECT_NE(trace.find("SCANNING -> AUTO_CONNECTING"), std::string::npos);
    EXPECT_NE(trace.find("mode=auto saved=1 budget=7000"), std::string::npos);
    EXPECT_EQ(trace.find("PRIVATE_NETWORK"), std::string::npos);

    diaglog::clear();
    session.noteDiagnostics(start + 8000);
    trace.assign(diaglog::data(), diaglog::size());
    EXPECT_NE(trace.find("nextAction/checkTimeouts=2"), std::string::npos);
    EXPECT_NE(trace.find("age_ms=8000"), std::string::npos);
    EXPECT_NE(trace.find("elapsed=8000 due=1"), std::string::npos);
    EXPECT_NE(trace.find("fired=0"), std::string::npos);
    drain(session, start + 8000);
    trace.assign(diaglog::data(), diaglog::size());
    EXPECT_NE(trace.find("timeout fired auto budget=7000 elapsed=8000"), std::string::npos);

    diaglog::clear();
    const auto network = seen("PRIVATE_NETWORK", -42);
    session.onScanResults(&network, 1, start + 8100);
    session.noteDiagnostics(start + 8100);
    trace.assign(diaglog::data(), diaglog::size());
    EXPECT_NE(trace.find("AUTO_CONNECTING -> NETWORK_LIST"), std::string::npos);
    EXPECT_NE(trace.find("joins=1 scans=1 fired=1"), std::string::npos);
    EXPECT_NE(trace.find("scan_count=1 target_seen=1"), std::string::npos);
    EXPECT_EQ(trace.find("PRIVATE_NETWORK"), std::string::npos);
  }
}

TEST(WifiSessionDiagnostics, ManualJoinKeepsStrongestTargetAndFailureInSnapshot) {
  WifiSession session;
  session.begin({}, 100);
  drain(session, 100);
  const std::vector<Network> found = {seen("PRIVATE_NETWORK", -80, false), seen("PRIVATE_NETWORK", -35, false)};
  session.onScanResults(found.data(), found.size(), 200);
  diaglog::clear();
  session.selectNetwork(0, 300);
  drain(session, 300);
  std::string trace(diaglog::data(), diaglog::size());
  EXPECT_NE(trace.find("mode=manual saved=0 budget=15000 expires=15300"), std::string::npos);
  drain(session, 15300);
  trace.assign(diaglog::data(), diaglog::size());
  EXPECT_NE(trace.find("timeout fired manual budget=15000 elapsed=15000"), std::string::npos);
  EXPECT_NE(trace.find("CONNECTING -> FAILED"), std::string::npos);
  // After old entries have rolled away, a heartbeat still explains the attempt.
  diaglog::clear();
  session.noteDiagnostics(600300);
  trace.assign(diaglog::data(), diaglog::size());
  EXPECT_NE(trace.find("state=FAILED since=15300"), std::string::npos);
  EXPECT_NE(trace.find("target_index=0 strongest_rssi=-35 scan_count=2 target_seen=1"), std::string::npos);
  EXPECT_EQ(trace.find("PRIVATE_NETWORK"), std::string::npos);
}

}  // namespace
