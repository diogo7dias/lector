// Retry on the failed-join screen: the same network again, with the same password,
// without walking back through the list.
#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include "WifiSession/WifiSession.h"

namespace {

using wifi_session::Action;
using wifi_session::ActionKind;
using wifi_session::Network;
using wifi_session::State;
using wifi_session::WifiSession;

Network seen(const char* ssid, int32_t rssi, bool encrypted = true) {
  Network network;
  snprintf(network.ssid, sizeof(network.ssid), "%s", ssid);
  network.rssi = rssi;
  network.isEncrypted = encrypted;
  return network;
}

std::vector<Action> drain(WifiSession& session, uint32_t nowMs) {
  std::vector<Action> actions;
  Action action;
  while (session.nextAction(nowMs, action)) actions.push_back(action);
  return actions;
}

// The list, scanned, with `saved` holding a credential.
WifiSession listed(const std::vector<Network>& found, const std::vector<std::string>& saved = {}) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.allowAutoConnect = false;
  startup.savedSsids = saved;
  session.begin(startup, 1000);
  drain(session, 1000);
  session.onScanResults(found.data(), found.size(), 2000);
  drain(session, 2000);
  return session;
}

}  // namespace

TEST(WifiSessionRetry, RejoinsASavedNetworkWithItsStoredPassword) {
  WifiSession session = listed({seen("home", -40)}, {"home"});
  session.selectNetwork(0, 3000);
  drain(session, 3000);
  session.onJoinFailed(4000);
  ASSERT_EQ(session.state(), State::FAILED);

  session.retryJoin(5000);

  EXPECT_EQ(session.state(), State::CONNECTING);
  const std::vector<Action> actions = drain(session, 5000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, ActionKind::JOIN);
  EXPECT_EQ(actions[0].ssid, "home");
  EXPECT_TRUE(actions[0].useSavedPassword);
}

TEST(WifiSessionRetry, RejoinsWithTheTypedPasswordAndStillOffersToSaveIt) {
  WifiSession session = listed({seen("cafe", -40)});
  session.selectNetwork(0, 3000);
  drain(session, 3000);
  session.onPasswordEntered(3500);
  drain(session, 3500);
  session.onJoinFailed(4000);

  session.retryJoin(5000);
  const std::vector<Action> actions = drain(session, 5000);
  ASSERT_EQ(actions.size(), 1u);
  EXPECT_EQ(actions[0].kind, ActionKind::JOIN);
  EXPECT_FALSE(actions[0].useSavedPassword);

  session.onJoinSucceeded(6000);
  EXPECT_EQ(session.state(), State::CONNECTED);
  EXPECT_TRUE(session.offersToSaveCredential());
}

TEST(WifiSessionRetry, DoesNothingOutsideAFailure) {
  WifiSession session = listed({seen("cafe", -40)});
  session.retryJoin(3000);
  EXPECT_EQ(session.state(), State::NETWORK_LIST);
  EXPECT_TRUE(drain(session, 3000).empty());
}

TEST(WifiSessionRetry, NeverRetriesInAutoOnly) {
  WifiSession session;
  wifi_session::Startup startup;
  startup.autoOnly = true;
  startup.savedSsids = {"home"};
  startup.lastConnectedSsid = "home";
  session.begin(startup, 1000);
  drain(session, 1000);
  session.onJoinFailed(2000);
  drain(session, 2000);
  const std::vector<Network> found = {};
  session.onScanResults(found.data(), found.size(), 3000);
  drain(session, 3000);
  ASSERT_EQ(session.state(), State::FAILED);

  session.retryJoin(4000);
  EXPECT_EQ(session.state(), State::FAILED);
  for (const Action& action : drain(session, 4000)) EXPECT_NE(action.kind, ActionKind::JOIN);
}
