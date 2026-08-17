#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lib/NearbyPositionSync/NearbyPositionProtocol.h"
#include "lib/NearbyPositionSync/NearbyPositionSession.h"

namespace {

using namespace nearby_position;

constexpr const char* OUR_HASH = "0123456789abcdef0123456789abcdef";
constexpr const char* THEIR_HASH = "fedcba9876543210fedcba9876543210";
constexpr std::array<uint8_t, MAC_BYTES> PEER_MAC = {0xaa, 0xbb, 0xcc, 0x01, 0x02, 0x03};
constexpr std::array<uint8_t, MAC_BYTES> STRANGER_MAC = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
constexpr std::array<uint8_t, MAC_BYTES> OUR_MAC = {0x24, 0x6f, 0x28, 0x77, 0x88, 0x99};

CompactPosition positionAt(const char* hash, const uint16_t spine, const uint16_t page, const float percentage) {
  CompactPosition pos;
  setDocumentHash(pos, hash);
  pos.spineIndex = spine;
  pos.pageNumber = page;
  pos.totalPages = 200;
  pos.percentageQ = percentageToQ(percentage);
  setXpath(pos, "/body/DocFragment[3]/body/div/p[11]/text().0");
  return pos;
}

/** Drains every action the session wants to take at `nowMs`. */
std::vector<Action> drain(SyncSession& session, const uint32_t nowMs) {
  std::vector<Action> actions;
  Action action;
  while (session.nextAction(nowMs, action)) actions.push_back(action);
  return actions;
}

bool contains(const std::vector<Action>& actions, const ActionKind kind) {
  for (const Action& action : actions) {
    if (action.kind == kind) return true;
  }
  return false;
}

PacketView packetFrom(const std::array<uint8_t, MAC_BYTES>& mac, const PacketType type) {
  PacketView view;
  view.type = type;
  view.deviceMac = mac;
  return view;
}

/** Runs a session up to the point where both sides know each other's position. */
SyncSession pairedSession(const CompactPosition& localPosition, const CompactPosition& peerPosition, uint32_t& nowMs) {
  SyncSession session;
  session.begin(localPosition, nowMs);
  drain(session, nowMs);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = peerPosition;
  session.onPacket(hello, nowMs);
  drain(session, nowMs);

  PacketView position = packetFrom(PEER_MAC, PacketType::POSITION);
  position.position = peerPosition;
  session.onPacket(position, nowMs);
  drain(session, nowMs);

  PacketView ack = packetFrom(PEER_MAC, PacketType::ACK);
  session.onPacket(ack, nowMs);
  drain(session, nowMs);
  return session;
}

}  // namespace

TEST(NearbyPositionSession, BroadcastsHelloWhileSearching) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);

  EXPECT_EQ(session.state(), SyncState::SEARCHING);
  EXPECT_TRUE(contains(drain(session, now), ActionKind::BROADCAST_HELLO));

  // Nothing to do again until the interval has elapsed, so the radio is not
  // hammered on every loop iteration.
  EXPECT_TRUE(drain(session, now + 1).empty());
  EXPECT_TRUE(contains(drain(session, now + HELLO_INTERVAL_MS), ActionKind::BROADCAST_HELLO));
}

TEST(NearbyPositionSession, GreetsAPeerReadingTheSameBook) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  drain(session, now);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = positionAt(OUR_HASH, 5, 4, 0.31f);
  session.onPacket(hello, now);

  const std::vector<Action> actions = drain(session, now);
  EXPECT_TRUE(contains(actions, ActionKind::SEND_NAME));
  EXPECT_TRUE(contains(actions, ActionKind::SEND_POSITION));
  for (const Action& action : actions) {
    if (action.kind != ActionKind::BROADCAST_HELLO) EXPECT_EQ(action.peerMac, PEER_MAC);
  }
  EXPECT_TRUE(session.hasPeer());
  EXPECT_EQ(session.peerMac(), PEER_MAC);
}

TEST(NearbyPositionSession, ReportsAPeerReadingADifferentBook) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  drain(session, now);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = positionAt(THEIR_HASH, 5, 4, 0.31f);
  session.onPacket(hello, now);

  EXPECT_EQ(session.state(), SyncState::BOOK_MISMATCH);
  // A mismatch is a dead end, not a state to keep transmitting from.
  EXPECT_TRUE(drain(session, now + HELLO_INTERVAL_MS * 4).empty());
  EXPECT_FALSE(session.hasPeer());
}

TEST(NearbyPositionSession, AcknowledgesAndStoresThePeerPosition) {
  uint32_t now = 1000;
  const CompactPosition peerPosition = positionAt(OUR_HASH, 9, 12, 0.62f);
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  drain(session, now);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = peerPosition;
  session.onPacket(hello, now);
  drain(session, now);

  PacketView position = packetFrom(PEER_MAC, PacketType::POSITION);
  position.position = peerPosition;
  session.onPacket(position, now);

  EXPECT_TRUE(contains(drain(session, now), ActionKind::SEND_ACK));
  EXPECT_TRUE(session.hasPeerPosition());
  EXPECT_EQ(session.peerPosition().spineIndex, 9);
  EXPECT_EQ(session.peerPosition().pageNumber, 12);
  EXPECT_EQ(session.state(), SyncState::COMPARING);
}

TEST(NearbyPositionSession, RemembersThePeerName) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  drain(session, now);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = positionAt(OUR_HASH, 9, 12, 0.62f);
  session.onPacket(hello, now);

  PacketView name = packetFrom(PEER_MAC, PacketType::NAME);
  name.deviceName = "Lector-4B2C";
  session.onPacket(name, now);

  EXPECT_EQ(session.peerName(), "Lector-4B2C");
}

TEST(NearbyPositionSession, ResendsThePositionUntilItIsAcknowledged) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  drain(session, now);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = positionAt(OUR_HASH, 9, 12, 0.62f);
  session.onPacket(hello, now);
  ASSERT_TRUE(contains(drain(session, now), ActionKind::SEND_POSITION));

  now += POSITION_RETRY_INTERVAL_MS;
  EXPECT_TRUE(contains(drain(session, now), ActionKind::SEND_POSITION));

  PacketView ack = packetFrom(PEER_MAC, PacketType::ACK);
  session.onPacket(ack, now);
  drain(session, now);

  now += POSITION_RETRY_INTERVAL_MS * 3;
  EXPECT_FALSE(contains(drain(session, now), ActionKind::SEND_POSITION));
}

TEST(NearbyPositionSession, TimesOutWhenNoPeerAnswers) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);

  drain(session, now + SEARCH_TIMEOUT_MS - 1);
  EXPECT_EQ(session.state(), SyncState::SEARCHING);

  drain(session, now + SEARCH_TIMEOUT_MS);
  EXPECT_EQ(session.state(), SyncState::TIMED_OUT);
  EXPECT_TRUE(drain(session, now + SEARCH_TIMEOUT_MS * 2).empty());
}

TEST(NearbyPositionSession, ReportsAPeerThatGoesQuietMidSync) {
  uint32_t now = 1000;
  SyncSession session = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);
  ASSERT_EQ(session.state(), SyncState::COMPARING);

  drain(session, now + PEER_TIMEOUT_MS - 1);
  EXPECT_EQ(session.state(), SyncState::COMPARING);

  drain(session, now + PEER_TIMEOUT_MS);
  EXPECT_EQ(session.state(), SyncState::PEER_LOST);
}

TEST(NearbyPositionSession, TakingThePeerPositionEndsTheSession) {
  uint32_t now = 1000;
  SyncSession session = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);

  session.takePeerPosition(now);
  EXPECT_EQ(session.state(), SyncState::APPLIED);
  EXPECT_TRUE(drain(session, now + PEER_TIMEOUT_MS * 2).empty());
}

TEST(NearbyPositionSession, SharingPushesTheLocalPositionAndWaitsForTheAck) {
  uint32_t now = 1000;
  SyncSession session = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);

  session.sharePosition(now);
  EXPECT_EQ(session.state(), SyncState::SHARING);
  ASSERT_TRUE(contains(drain(session, now), ActionKind::SEND_APPLY));

  // Unacknowledged, so it goes again rather than silently doing nothing.
  now += POSITION_RETRY_INTERVAL_MS;
  EXPECT_TRUE(contains(drain(session, now), ActionKind::SEND_APPLY));

  PacketView ack = packetFrom(PEER_MAC, PacketType::ACK);
  session.onPacket(ack, now);
  EXPECT_EQ(session.state(), SyncState::SHARED);
  EXPECT_TRUE(drain(session, now + POSITION_RETRY_INTERVAL_MS * 3).empty());
}

TEST(NearbyPositionSession, AnIncomingApplyWaitsForTheReader) {
  uint32_t now = 1000;
  SyncSession session = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);

  PacketView apply = packetFrom(PEER_MAC, PacketType::APPLY);
  apply.position = positionAt(OUR_HASH, 14, 2, 0.81f);
  session.onPacket(apply, now);

  // The sender is told the request arrived; the position still does not move
  // until the reader on this device confirms it.
  EXPECT_TRUE(contains(drain(session, now), ActionKind::SEND_ACK));
  EXPECT_EQ(session.state(), SyncState::APPLY_REQUESTED);
  EXPECT_EQ(session.peerPosition().spineIndex, 14);

  session.takePeerPosition(now);
  EXPECT_EQ(session.state(), SyncState::APPLIED);
}

TEST(NearbyPositionSession, IgnoresPacketsFromOtherDevicesOncePaired) {
  uint32_t now = 1000;
  SyncSession session = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);

  PacketView intruder = packetFrom(STRANGER_MAC, PacketType::POSITION);
  intruder.position = positionAt(OUR_HASH, 99, 99, 0.99f);
  session.onPacket(intruder, now);

  EXPECT_EQ(session.peerMac(), PEER_MAC);
  EXPECT_EQ(session.peerPosition().spineIndex, 9);
  EXPECT_FALSE(contains(drain(session, now), ActionKind::SEND_ACK));
}

TEST(NearbyPositionSession, IgnoresOurOwnEchoedBroadcast) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  session.setLocalMac(PEER_MAC);
  drain(session, now);

  PacketView echo = packetFrom(PEER_MAC, PacketType::HELLO);
  echo.position = positionAt(OUR_HASH, 3, 10, 0.25f);
  session.onPacket(echo, now);

  EXPECT_FALSE(session.hasPeer());
  EXPECT_EQ(session.state(), SyncState::SEARCHING);
}

TEST(NearbyPositionSession, CountsWhereEachPacketCameFrom) {
  // These counts are shown on the searching screen. A reader that hears only its
  // own broadcasts and one that hears nothing look the same on the panel, and the
  // counts are the only thing that separates them.
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  session.setLocalMac(OUR_MAC);
  drain(session, now);

  PacketView echo = packetFrom(OUR_MAC, PacketType::HELLO);
  echo.position = positionAt(OUR_HASH, 3, 10, 0.25f);
  session.onPacket(echo, now);
  session.onPacket(echo, now);
  EXPECT_EQ(session.packetsFromSelf(), 2);
  EXPECT_EQ(session.packetsFromPeer(), 0);

  PacketView hello = packetFrom(PEER_MAC, PacketType::HELLO);
  hello.position = positionAt(OUR_HASH, 9, 12, 0.62f);
  session.onPacket(hello, now);
  ASSERT_TRUE(session.hasPeer());
  EXPECT_EQ(session.packetsFromPeer(), 1);

  session.onPacket(packetFrom(STRANGER_MAC, PacketType::HELLO), now);
  EXPECT_EQ(session.packetsFromOthers(), 1);
  EXPECT_EQ(session.packetsFromPeer(), 1);
}

TEST(NearbyPositionSession, KnowsWhichSideIsFurtherAlong) {
  uint32_t now = 1000;
  SyncSession behind = pairedSession(positionAt(OUR_HASH, 3, 10, 0.25f), positionAt(OUR_HASH, 9, 12, 0.62f), now);
  EXPECT_TRUE(behind.peerIsFurtherAlong());

  now = 1000;
  SyncSession ahead = pairedSession(positionAt(OUR_HASH, 9, 12, 0.62f), positionAt(OUR_HASH, 3, 10, 0.25f), now);
  EXPECT_FALSE(ahead.peerIsFurtherAlong());

  now = 1000;
  SyncSession level = pairedSession(positionAt(OUR_HASH, 5, 5, 0.5f), positionAt(OUR_HASH, 5, 5, 0.5f), now);
  EXPECT_FALSE(level.peerIsFurtherAlong());
  EXPECT_TRUE(level.positionsMatch());
}

TEST(NearbyPositionSession, PairsWithAReaderThatIsAlreadyTalkingToUs) {
  // The other reader heard our announcement, paired, and stopped announcing
  // itself: from then on it only sends its position, straight to us. Waiting for
  // an announcement that will never come again leaves both readers searching.
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  session.setLocalMac(OUR_MAC);
  drain(session, now);

  PacketView position = packetFrom(PEER_MAC, PacketType::POSITION);
  position.position = positionAt(OUR_HASH, 9, 12, 0.62f);
  session.onPacket(position, now);

  EXPECT_TRUE(session.hasPeer());
  EXPECT_TRUE(session.hasPeerPosition());
  EXPECT_EQ(session.state(), SyncState::COMPARING);
  EXPECT_TRUE(contains(drain(session, now), ActionKind::SEND_ACK));
}

TEST(NearbyPositionSession, ARequestToMoveAlsoPairsAnUnpairedReader) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  session.setLocalMac(OUR_MAC);
  drain(session, now);

  PacketView apply = packetFrom(PEER_MAC, PacketType::APPLY);
  apply.position = positionAt(OUR_HASH, 9, 12, 0.62f);
  session.onPacket(apply, now);

  EXPECT_TRUE(session.hasPeer());
  EXPECT_EQ(session.state(), SyncState::APPLY_REQUESTED);
}

TEST(NearbyPositionSession, ADifferentBookIsStillRefusedWhenPairingOnAPosition) {
  uint32_t now = 1000;
  SyncSession session;
  session.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  session.setLocalMac(OUR_MAC);
  drain(session, now);

  PacketView position = packetFrom(PEER_MAC, PacketType::POSITION);
  position.position = positionAt(THEIR_HASH, 9, 12, 0.62f);
  session.onPacket(position, now);

  EXPECT_EQ(session.state(), SyncState::BOOK_MISMATCH);
  EXPECT_FALSE(session.hasPeerPosition());
}

TEST(NearbyPositionSession, TwoReadersPairWhenOnlyOneOfThemHeardAnAnnouncement) {
  // The real failure on two devices: whoever pairs first stops announcing itself,
  // so if its own announcement was lost the other reader searches until it gives
  // up while the first talks to a reader that is not listening.
  const std::array<uint8_t, MAC_BYTES> LEFT_MAC = OUR_MAC;
  const std::array<uint8_t, MAC_BYTES> RIGHT_MAC = PEER_MAC;

  uint32_t now = 1000;
  SyncSession left;
  SyncSession right;
  left.begin(positionAt(OUR_HASH, 3, 10, 0.25f), now);
  right.begin(positionAt(OUR_HASH, 9, 12, 0.62f), now);
  left.setLocalMac(LEFT_MAC);
  right.setLocalMac(RIGHT_MAC);

  bool leftHelloDelivered = false;
  const auto deliver = [&](SyncSession& from, const std::array<uint8_t, MAC_BYTES>& fromMac, SyncSession& to) {
    for (const Action& action : drain(from, now)) {
      PacketView packet;
      packet.deviceMac = fromMac;
      packet.position = from.localPosition();
      switch (action.kind) {
        case ActionKind::BROADCAST_HELLO:
          packet.type = PacketType::HELLO;
          // The first announcement from the left reader never arrives, which is
          // what puts the two sides out of step.
          if (&from == &left && !leftHelloDelivered) {
            leftHelloDelivered = true;
            continue;
          }
          break;
        case ActionKind::SEND_NAME:
          packet.type = PacketType::NAME;
          packet.deviceName = "Reader";
          break;
        case ActionKind::SEND_POSITION:
          packet.type = PacketType::POSITION;
          break;
        case ActionKind::SEND_ACK:
          packet.type = PacketType::ACK;
          break;
        case ActionKind::SEND_APPLY:
          packet.type = PacketType::APPLY;
          break;
      }
      to.onPacket(packet, now);
    }
  };

  for (int step = 0; step < 200; step++) {
    deliver(right, RIGHT_MAC, left);
    deliver(left, LEFT_MAC, right);
    if (left.state() == SyncState::COMPARING && right.state() == SyncState::COMPARING) break;
    now += 100;
  }

  EXPECT_EQ(left.state(), SyncState::COMPARING);
  EXPECT_EQ(right.state(), SyncState::COMPARING);
  EXPECT_TRUE(left.hasPeerPosition());
  EXPECT_TRUE(right.hasPeerPosition());
}
