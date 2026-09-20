#include <gtest/gtest.h>

#include <array>
#include <string_view>

#include "lib/NearbyPositionSync/NearbyPositionReceive.h"
#include "lib/NearbyPositionSync/NearbyPositionSession.h"

namespace {
using namespace nearby_position;
constexpr std::string_view HASH = "0123456789abcdef0123456789abcdef";
constexpr std::string_view OTHER_HASH = "fedcba9876543210fedcba9876543210";
constexpr std::array<uint8_t, MAC_BYTES> SENDER = {1, 2, 3, 4, 5, 6};
constexpr std::array<uint8_t, MAC_BYTES> RECEIVER = {6, 5, 4, 3, 2, 1};

PacketView positionPacket() {
  PacketView packet;
  packet.type = PacketType::POSITION;
  packet.deviceMac = SENDER;
  setDocumentHash(packet.position, std::string(HASH));
  setXpath(packet.position, "/body/DocFragment[3]/body/div[1]/p[7]/text()[1].23");
  packet.position.spineIndex = 2;
  packet.position.pageNumber = 41;
  packet.position.totalPages = 90;
  packet.position.percentageQ = 420000;
  return packet;
}

TEST(NearbyPositionReceive, MatchesOnlyTheFullDocumentHash) {
  EXPECT_TRUE(matchesDocumentHash(HASH, HASH));
  EXPECT_FALSE(matchesDocumentHash(HASH, OTHER_HASH));
  EXPECT_FALSE(matchesDocumentHash(HASH.substr(0, 31), HASH));
  EXPECT_FALSE(matchesDocumentHash(std::string(32, 'x'), std::string(32, 'x')));
}

TEST(NearbyPositionReceive, NoCandidatesNeverSelectsABook) {
  const std::array<std::string_view, 0> candidates{};
  bool matched = false;
  for (const auto hash : candidates) matched |= matchesDocumentHash(HASH, hash);
  EXPECT_FALSE(matched);
  EXPECT_FALSE(matchesDocumentHash(HASH, {}));
}

TEST(NearbyPositionReceive, HashMismatchRefusesBothPositionAndWrite) {
  EXPECT_FALSE(receivablePosition(positionPacket(), OTHER_HASH));
  const CachedPosition mapped{2, 9, 20, 1234};
  EXPECT_FALSE(receivedProgressRecord(HASH, OTHER_HASH, mapped, 8, true, true));
}

TEST(NearbyPositionReceive, AcceptsPositionAndApplyButNotHello) {
  auto packet = positionPacket();
  EXPECT_TRUE(receivablePosition(packet, HASH));
  packet.type = PacketType::APPLY;
  EXPECT_TRUE(receivablePosition(packet, HASH));
  packet.type = PacketType::HELLO;
  EXPECT_FALSE(receivablePosition(packet, HASH));
}

TEST(NearbyPositionReceive, TruncatedWirePayloadNeverReachesConversion) {
  const auto packet = positionPacket();
  std::array<uint8_t, MAX_PACKET_BYTES> wire{};
  size_t length = 0;
  ASSERT_TRUE(encodePacket(packet.type, SENDER.data(), packet.position, "", wire.data(), wire.size(), length));
  PacketView decoded;
  for (size_t size = 0; size < length; ++size) {
    EXPECT_FALSE(decodePacket(wire.data(), size, decoded)) << size;
  }
  // Even with a forged header length, a short fixed position is invalid.
  wire[6] = 32;
  wire[7] = 0;
  EXPECT_FALSE(decodePacket(wire.data(), PACKET_HEADER_BYTES + 32, decoded));
}

TEST(NearbyPositionReceive, MalformedXpathLengthIsRejectedByTheWireDecoder) {
  const auto packet = positionPacket();
  std::array<uint8_t, MAX_PACKET_BYTES> wire{};
  size_t length = 0;
  ASSERT_TRUE(encodePacket(packet.type, SENDER.data(), packet.position, "", wire.data(), wire.size(), length));
  // Final byte of fixed payload is xpath length when anchor is empty.
  wire[PACKET_HEADER_BYTES + 48] = MAX_XPATH_BYTES + 1;
  PacketView decoded;
  EXPECT_FALSE(decodePacket(wire.data(), length, decoded));
}

TEST(NearbyPositionReceive, RejectsMissingTruncatedOrOverflowingXpath) {
  auto packet = positionPacket();
  setXpath(packet.position, "");
  EXPECT_FALSE(receivablePosition(packet, HASH));
  setXpath(packet.position, "/body/DocFragment[99999999999999999]/body/p[1]/text().0");
  EXPECT_FALSE(receivablePosition(packet, HASH));
  setXpath(packet.position, "/body/DocFragment[3]/body/p[1]/text().99999999999999999");
  EXPECT_FALSE(receivablePosition(packet, HASH));
  setXpath(packet.position, "/body/DocFragment[3]/body/" + std::string(120, 'p'));
  EXPECT_FALSE(receivablePosition(packet, HASH));
  packet.position.xpath.fill('x');
  EXPECT_FALSE(receivablePosition(packet, HASH));
}

TEST(NearbyPositionReceive, RejectsInvalidWirePageAndPercentage) {
  auto packet = positionPacket();
  packet.position.pageNumber = packet.position.totalPages;
  EXPECT_FALSE(receivablePosition(packet, HASH));
  packet.position.pageNumber = 0;
  packet.position.totalPages = 0;
  EXPECT_FALSE(receivablePosition(packet, HASH));
  packet.position.totalPages = 1;
  packet.position.percentageQ = PERCENTAGE_SCALE + 1;
  EXPECT_FALSE(receivablePosition(packet, HASH));
}

TEST(NearbyPositionReceive, WritesReceiverPageAndOffsetInReaderByteOrder) {
  const auto packet = positionPacket();
  const CachedPosition mapped{2, 9, 20, 0x12345678};
  const auto bytes = receivedProgressRecord(HASH, HASH, mapped, 8, true, true);
  ASSERT_TRUE(bytes);
  const std::array<uint8_t, 10> expected{2, 0, 9, 0, 20, 0, 0x78, 0x56, 0x34, 0x12};
  EXPECT_EQ(*bytes, expected);
  const auto loaded = readCachedPosition(bytes->data(), bytes->size());
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->spine, 2);
  EXPECT_EQ(loaded->page, 9);
  EXPECT_NE(loaded->page, packet.position.pageNumber);
  EXPECT_EQ(loaded->pages, 20);
  EXPECT_EQ(loaded->offset, 0x12345678u);
}

TEST(NearbyPositionReceive, NoProvenAnchorOrLocalPaginationMeansNoWrite) {
  CachedPosition mapped{2, 9, 20, 1234};
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, false, true));
  mapped.offset.reset();
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, true));
  mapped.offset = 1234;
  mapped.pages = 0;
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, false));
  mapped.pages = 9;
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, true));
  mapped.pages = 20;
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 2, true, true));
}

TEST(NearbyPositionReceive, MissingSectionUsesOffsetOnRebuildButExistingUnmappedSectionRefuses) {
  const CachedPosition mapped{3, 0, 0, 9876};
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, false));
  const auto bytes = receivedProgressRecord(HASH, HASH, mapped, 8, true, false, true);
  ASSERT_TRUE(bytes);
  const auto loaded = readCachedPosition(bytes->data(), bytes->size());
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->spine, 3);
  EXPECT_EQ(loaded->page, 0);
  EXPECT_EQ(loaded->pages, 0);
  EXPECT_EQ(loaded->offset, 9876u);
}

TEST(NearbyPositionReceive, RepeatedReceivesCompareTheSavedOffsetWithoutOpeningTheBook) {
  const CachedPosition first{3, 0, 0, 9876};
  const auto bytes = receivedProgressRecord(HASH, HASH, first, 8, true, false, true);
  ASSERT_TRUE(bytes);
  const auto local = readCachedPosition(bytes->data(), bytes->size());
  ASSERT_TRUE(local);
  EXPECT_EQ(resolveCachedPosition(*local, {3, 0, 0, 9000}), Resolution::KeepLocal);
  EXPECT_EQ(resolveCachedPosition(*local, {3, 0, 0, 10000}), Resolution::TakePeer);
}

TEST(NearbyPositionReceive, PartialSectionNeedsAProvenPageEvenWhenFinalCountIsUnknown) {
  const CachedPosition mapped{3, 5, 0, 9876};
  EXPECT_TRUE(receivedProgressRecord(HASH, HASH, mapped, 8, true, true));
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, false));
  EXPECT_FALSE(receivedProgressRecord(HASH, HASH, mapped, 8, true, false, true));
}

TEST(NearbyPositionReceive, AcceptsExistingReaderRecordSizesOnly) {
  std::array<uint8_t, 11> bytes{2, 0, 9, 0, 20, 0, 0x78, 0x56, 0x34, 0x12, 0};
  for (size_t size = 0; size <= bytes.size(); ++size) {
    EXPECT_EQ(readCachedPosition(bytes.data(), size).has_value(), size == 4 || size == 6 || size == 10) << size;
  }
  EXPECT_FALSE(readCachedPosition(nullptr, 10));
  bytes[2] = 0xff;
  bytes[3] = 0xff;
  EXPECT_FALSE(readCachedPosition(bytes.data(), 10));
}

TEST(NearbyPositionReceive, KeepsFurthestUsingResolvedDocumentOrder) {
  const CachedPosition local{2, 9, 20, 1000};
  EXPECT_EQ(resolveCachedPosition(local, {2, 7, 90, 999}), Resolution::KeepLocal);
  EXPECT_EQ(resolveCachedPosition(local, {2, 41, 90, 1000}), Resolution::Same);
  EXPECT_EQ(resolveCachedPosition(local, {2, 41, 90, 1001}), Resolution::TakePeer);
  EXPECT_EQ(resolveCachedPosition(local, {1, 90, 100, 90000}), Resolution::KeepLocal);
  EXPECT_EQ(resolveCachedPosition(local, {3, 0, 5, 0}), Resolution::TakePeer);
}

TEST(NearbyPositionReceive, ExistingSenderCompletesWithoutMovingWhenReceiverEchoesSavedPosition) {
  const auto original = positionPacket();
  SyncSession sender;
  sender.begin(original.position, 0);
  sender.setLocalMac(SENDER);
  auto hello = original;
  hello.type = PacketType::HELLO;
  hello.deviceMac = RECEIVER;
  sender.onPacket(hello, 1);
  Action action;
  while (sender.nextAction(1, action)) {
  }
  auto echo = original;
  echo.deviceMac = RECEIVER;
  sender.onPacket(echo, 2);
  auto ack = hello;
  ack.type = PacketType::ACK;
  sender.onPacket(ack, 2);
  while (sender.nextAction(2, action)) {
  }
  EXPECT_EQ(sender.state(), SyncState::SHARED);
  EXPECT_EQ(sender.resolution(), Resolution::Same);
}
}  // namespace
