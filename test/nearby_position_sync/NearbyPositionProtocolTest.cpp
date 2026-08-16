#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "lib/NearbyPositionSync/NearbyPositionProtocol.h"

namespace {

using namespace nearby_position;

// The wire format is shared with CrossInk's Nearby Position Sync, so these
// literal offsets and magic bytes are the compatibility contract, not an
// implementation detail. A device running either firmware must be able to read
// the other's packets.
constexpr uint8_t LOCAL_MAC[MAC_BYTES] = {0x24, 0x6f, 0x28, 0x11, 0x22, 0x33};
constexpr const char* HASH_A = "0123456789abcdef0123456789abcdef";
constexpr const char* HASH_B = "fedcba9876543210fedcba9876543210";

CompactPosition samplePosition(const char* documentHash = HASH_A) {
  CompactPosition pos;
  setDocumentHash(pos, documentHash);
  pos.percentageQ = 421337;
  pos.spineIndex = 7;
  pos.pageNumber = 42;
  pos.totalPages = 118;
  pos.paragraphIndex = 9;
  pos.hasParagraphIndex = true;
  pos.liIndex = 3;
  pos.hasLiIndex = true;
  setAnchor(pos, "chapter-7-start");
  setXpath(pos, "/body/DocFragment[8]/body/div/p[9]/text().0");
  return pos;
}

std::vector<uint8_t> encodeOrDie(const PacketType type, const CompactPosition& pos, const std::string& deviceName) {
  std::array<uint8_t, MAX_PACKET_BYTES> buffer = {};
  size_t length = 0;
  EXPECT_TRUE(encodePacket(type, LOCAL_MAC, pos, deviceName, buffer.data(), buffer.size(), length));
  return std::vector<uint8_t>(buffer.begin(), buffer.begin() + length);
}

}  // namespace

TEST(NearbyPositionProtocol, PositionPacketRoundTrips) {
  const CompactPosition sent = samplePosition();
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::POSITION, sent, "");

  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(view.type, PacketType::POSITION);
  EXPECT_EQ(0, std::memcmp(view.deviceMac.data(), LOCAL_MAC, MAC_BYTES));

  const CompactPosition& got = view.position;
  EXPECT_STREQ(got.documentHash.data(), HASH_A);
  EXPECT_EQ(got.percentageQ, sent.percentageQ);
  EXPECT_EQ(got.spineIndex, sent.spineIndex);
  EXPECT_EQ(got.pageNumber, sent.pageNumber);
  EXPECT_EQ(got.totalPages, sent.totalPages);
  EXPECT_EQ(got.paragraphIndex, sent.paragraphIndex);
  EXPECT_TRUE(got.hasParagraphIndex);
  EXPECT_EQ(got.liIndex, sent.liIndex);
  EXPECT_TRUE(got.hasLiIndex);
  EXPECT_STREQ(got.anchor.data(), sent.anchor.data());
  EXPECT_STREQ(got.xpath.data(), sent.xpath.data());
}

TEST(NearbyPositionProtocol, HeaderMatchesTheCrossInkLayout) {
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::HELLO, samplePosition(), "");

  ASSERT_GE(packet.size(), PACKET_HEADER_BYTES);
  EXPECT_EQ(packet[0], 'C');
  EXPECT_EQ(packet[1], 'I');
  EXPECT_EQ(packet[2], 'B');
  EXPECT_EQ(packet[3], 'P');
  EXPECT_EQ(packet[4], PROTOCOL_VERSION);
  EXPECT_EQ(packet[5], static_cast<uint8_t>(PacketType::HELLO));
  // Payload length is little-endian, and a HELLO carries only the hash.
  EXPECT_EQ(packet[6], DOCUMENT_HASH_BYTES);
  EXPECT_EQ(packet[7], 0);
  EXPECT_EQ(0, std::memcmp(packet.data() + 8, LOCAL_MAC, MAC_BYTES));
  EXPECT_EQ(packet.size(), PACKET_HEADER_BYTES + DOCUMENT_HASH_BYTES);
}

TEST(NearbyPositionProtocol, HelloCarriesTheDocumentHash) {
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::HELLO, samplePosition(HASH_B), "");

  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(view.type, PacketType::HELLO);
  EXPECT_STREQ(view.position.documentHash.data(), HASH_B);
}

TEST(NearbyPositionProtocol, NamePacketRoundTripsAndIsBounded) {
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::NAME, samplePosition(), "Lector-2233");

  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(view.type, PacketType::NAME);
  EXPECT_EQ(view.deviceName, "Lector-2233");

  // A name longer than the wire allows is truncated, never overflowed.
  const std::string tooLong(MAX_DEVICE_NAME_BYTES + 17, 'x');
  const std::vector<uint8_t> truncated = encodeOrDie(PacketType::NAME, samplePosition(), tooLong);
  PacketView truncatedView;
  ASSERT_TRUE(decodePacket(truncated.data(), truncated.size(), truncatedView));
  EXPECT_EQ(truncatedView.deviceName.size(), MAX_DEVICE_NAME_BYTES);
}

TEST(NearbyPositionProtocol, AckCarriesNoPayload) {
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::ACK, samplePosition(), "");

  EXPECT_EQ(packet.size(), PACKET_HEADER_BYTES);
  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(view.type, PacketType::ACK);
}

TEST(NearbyPositionProtocol, RejectsForeignAndCorruptPackets) {
  const std::vector<uint8_t> valid = encodeOrDie(PacketType::POSITION, samplePosition(), "");
  PacketView view;

  // Truncated at every length short of the full packet.
  for (size_t length = 0; length < valid.size(); length++) {
    EXPECT_FALSE(decodePacket(valid.data(), length, view)) << "accepted a packet truncated to " << length << " bytes";
  }

  std::vector<uint8_t> wrongMagic = valid;
  wrongMagic[1] = 'X';
  EXPECT_FALSE(decodePacket(wrongMagic.data(), wrongMagic.size(), view));

  std::vector<uint8_t> wrongVersion = valid;
  wrongVersion[4] = PROTOCOL_VERSION + 1;
  EXPECT_FALSE(decodePacket(wrongVersion.data(), wrongVersion.size(), view));

  std::vector<uint8_t> unknownType = valid;
  unknownType[5] = 0x7E;
  EXPECT_FALSE(decodePacket(unknownType.data(), unknownType.size(), view));

  // A declared payload length that overruns the buffer must not be trusted.
  std::vector<uint8_t> lyingLength = valid;
  lyingLength[6] = 0xFF;
  lyingLength[7] = 0x00;
  EXPECT_FALSE(decodePacket(lyingLength.data(), lyingLength.size(), view));

  // Trailing bytes past the declared payload mean the packet is not what it claims.
  std::vector<uint8_t> trailing = valid;
  trailing.push_back(0x00);
  EXPECT_FALSE(decodePacket(trailing.data(), trailing.size(), view));

  EXPECT_FALSE(decodePacket(nullptr, PACKET_HEADER_BYTES, view));
}

TEST(NearbyPositionProtocol, RejectsAnOversizedStringLength) {
  std::vector<uint8_t> packet = encodeOrDie(PacketType::POSITION, samplePosition(), "");

  // The anchor length byte sits right after the fixed position fields.
  constexpr size_t anchorLengthOffset = PACKET_HEADER_BYTES + DOCUMENT_HASH_BYTES + 4 + 2 + 2 + 2 + 1 + 2 + 2;
  ASSERT_LT(anchorLengthOffset, packet.size());
  packet[anchorLengthOffset] = MAX_ANCHOR_BYTES + 1;

  PacketView view;
  EXPECT_FALSE(decodePacket(packet.data(), packet.size(), view));
}

TEST(NearbyPositionProtocol, ClampsOutOfRangeValues) {
  CompactPosition pos = samplePosition();
  pos.percentageQ = PERCENTAGE_SCALE * 4;
  pos.totalPages = 0;
  const std::vector<uint8_t> packet = encodeOrDie(PacketType::POSITION, pos, "");

  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(view.position.percentageQ, PERCENTAGE_SCALE);
  EXPECT_EQ(view.position.totalPages, 1);
  EXPECT_FLOAT_EQ(percentageFromQ(view.position.percentageQ), 1.0f);
}

TEST(NearbyPositionProtocol, RefusesToEncodeAMalformedDocumentHash) {
  CompactPosition pos = samplePosition();
  setDocumentHash(pos, "tooshort");

  std::array<uint8_t, MAX_PACKET_BYTES> buffer = {};
  size_t length = 0;
  EXPECT_FALSE(encodePacket(PacketType::POSITION, LOCAL_MAC, pos, "", buffer.data(), buffer.size(), length));
  EXPECT_FALSE(encodePacket(PacketType::HELLO, LOCAL_MAC, pos, "", buffer.data(), buffer.size(), length));
}

TEST(NearbyPositionProtocol, TruncatesOverlongAnchorAndXpath) {
  CompactPosition pos = samplePosition();
  setAnchor(pos, std::string(MAX_ANCHOR_BYTES + 25, 'a'));
  setXpath(pos, std::string(MAX_XPATH_BYTES + 25, 'x'));

  const std::vector<uint8_t> packet = encodeOrDie(PacketType::POSITION, pos, "");
  PacketView view;
  ASSERT_TRUE(decodePacket(packet.data(), packet.size(), view));
  EXPECT_EQ(std::strlen(view.position.anchor.data()), MAX_ANCHOR_BYTES);
  EXPECT_EQ(std::strlen(view.position.xpath.data()), MAX_XPATH_BYTES);
  EXPECT_LE(packet.size(), MAX_PACKET_BYTES);
}

TEST(NearbyPositionProtocol, PercentageSurvivesTheQuantisedRoundTrip) {
  for (const float percentage : {0.0f, 0.125f, 0.5f, 0.9994f, 1.0f}) {
    const uint32_t quantised = percentageToQ(percentage);
    EXPECT_NEAR(percentageFromQ(quantised), percentage, 1e-6f);
  }
  EXPECT_EQ(percentageToQ(-3.0f), 0u);
  EXPECT_EQ(percentageToQ(7.5f), PERCENTAGE_SCALE);
}
