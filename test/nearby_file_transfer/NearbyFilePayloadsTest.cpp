#include <NearbyTransfer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "lib/NearbyFileTransfer/NearbyFilePayloads.h"

namespace {

using namespace nearby_file;

std::vector<uint8_t> encodeOffer(const OfferPayload& offer) {
  std::array<uint8_t, 256> buffer = {};
  size_t length = 0;
  EXPECT_TRUE(encodeOfferPayload(offer, buffer.data(), buffer.size(), length));
  return std::vector<uint8_t>(buffer.begin(), buffer.begin() + length);
}

}  // namespace

TEST(NearbyFilePayloads, OfferRoundTrips) {
  OfferPayload sent;
  sent.deviceName = "Lector-4B2C";
  sent.fileName = "Fear & Loathing.epub";
  sent.fileSize = 4823411;

  const std::vector<uint8_t> packet = encodeOffer(sent);
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.deviceName, sent.deviceName);
  EXPECT_EQ(got.fileName, sent.fileName);
  EXPECT_EQ(got.fileSize, sent.fileSize);
}

TEST(NearbyFilePayloads, OfferRejectsTruncatedInput) {
  OfferPayload sent;
  sent.deviceName = "Lector-4B2C";
  sent.fileName = "Book.epub";
  sent.fileSize = 1024;
  const std::vector<uint8_t> packet = encodeOffer(sent);

  // Everything up to the end of the filename is required. The group fields after
  // it are optional, since an offer from firmware that predates them stops right
  // there, so a cut at that exact boundary is a valid older offer rather than a
  // truncation.
  const size_t requiredLength = 8 + 1 + sent.deviceName.size() + 1 + sent.fileName.size();
  OfferPayload got;
  for (size_t length = 0; length < requiredLength; length++) {
    EXPECT_FALSE(decodeOfferPayload(packet.data(), length, got)) << "accepted an offer truncated to " << length;
  }
  for (size_t length = requiredLength + 1; length < packet.size(); length++) {
    EXPECT_FALSE(decodeOfferPayload(packet.data(), length, got)) << "accepted an offer truncated to " << length;
  }
}

TEST(NearbyFilePayloads, OfferRefusesALyingLengthByte) {
  OfferPayload sent;
  sent.deviceName = "Lector";
  sent.fileName = "Book.epub";
  sent.fileSize = 1024;
  std::vector<uint8_t> packet = encodeOffer(sent);

  // The device-name length sits right after the 8-byte size. Claiming more bytes
  // than arrived must be refused rather than read past the end.
  packet[8] = 0xFF;
  OfferPayload got;
  EXPECT_FALSE(decodeOfferPayload(packet.data(), packet.size(), got));
}

TEST(NearbyFilePayloads, OfferTruncatesOverlongNames) {
  OfferPayload sent;
  sent.deviceName = std::string(MAX_NAME_BYTES + 40, 'n');
  sent.fileName = std::string(MAX_OFFER_NAME_BYTES + 40, 'f') + ".epub";
  sent.fileSize = 10;

  const std::vector<uint8_t> packet = encodeOffer(sent);
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.deviceName.size(), MAX_NAME_BYTES);
  EXPECT_EQ(got.fileName.size(), MAX_OFFER_NAME_BYTES);
}

TEST(NearbyFilePayloads, NameRoundTripsAndIsBounded) {
  std::array<uint8_t, 64> buffer = {};
  size_t length = 0;
  ASSERT_TRUE(encodeNamePayload("Lector-9F01", buffer.data(), buffer.size(), length));

  std::string name;
  ASSERT_TRUE(decodeNamePayload(buffer.data(), length, name));
  EXPECT_EQ(name, "Lector-9F01");

  ASSERT_TRUE(encodeNamePayload(std::string(MAX_NAME_BYTES + 10, 'x'), buffer.data(), buffer.size(), length));
  ASSERT_TRUE(decodeNamePayload(buffer.data(), length, name));
  EXPECT_EQ(name.size(), MAX_NAME_BYTES);

  EXPECT_FALSE(decodeNamePayload(buffer.data(), 0, name));
}

TEST(NearbyFilePayloads, CompleteCarriesTheChecksumAndTheTotal) {
  std::array<uint8_t, 32> buffer = {};
  size_t length = 0;
  CompletePayload sent;
  sent.crc32 = 0xDEADBEEF;
  sent.totalBytes = 4823411;
  ASSERT_TRUE(encodeCompletePayload(sent, buffer.data(), buffer.size(), length));

  CompletePayload got;
  ASSERT_TRUE(decodeCompletePayload(buffer.data(), length, got));
  EXPECT_EQ(got.crc32, sent.crc32);
  EXPECT_EQ(got.totalBytes, sent.totalBytes);

  EXPECT_FALSE(decodeCompletePayload(buffer.data(), length - 1, got));
}

TEST(NearbyFilePayloads, ResultCarriesTheVerdict) {
  std::array<uint8_t, 8> buffer = {};
  size_t length = 0;
  bool success = false;

  ASSERT_TRUE(encodeResultPayload(true, buffer.data(), buffer.size(), length));
  ASSERT_TRUE(decodeResultPayload(buffer.data(), length, success));
  EXPECT_TRUE(success);

  ASSERT_TRUE(encodeResultPayload(false, buffer.data(), buffer.size(), length));
  ASSERT_TRUE(decodeResultPayload(buffer.data(), length, success));
  EXPECT_FALSE(success);

  EXPECT_FALSE(decodeResultPayload(buffer.data(), 0, success));
}

TEST(NearbyFilePayloads, RefusesToEncodeIntoTooSmallABuffer) {
  std::array<uint8_t, 4> tiny = {};
  size_t length = 0;

  OfferPayload offer;
  offer.deviceName = "Lector";
  offer.fileName = "Book.epub";
  offer.fileSize = 10;
  EXPECT_FALSE(encodeOfferPayload(offer, tiny.data(), tiny.size(), length));

  CompletePayload complete;
  EXPECT_FALSE(encodeCompletePayload(complete, tiny.data(), tiny.size(), length));
  EXPECT_FALSE(encodeNamePayload("a name longer than four", tiny.data(), tiny.size(), length));
}

TEST(NearbyFilePayloads, OfferCarriesTheGroupAndTheFolder) {
  OfferPayload sent;
  sent.deviceName = "Lector-4B2C";
  sent.fileName = "Literata_14.cpfont";
  sent.fileSize = 51200;
  sent.folder = ".fonts/Literata";
  sent.groupIndex = 2;
  sent.groupCount = 6;
  sent.groupTotalBytes = 307200;

  const std::vector<uint8_t> packet = encodeOffer(sent);
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.folder, sent.folder);
  EXPECT_EQ(got.groupIndex, sent.groupIndex);
  EXPECT_EQ(got.groupCount, sent.groupCount);
  EXPECT_EQ(got.groupTotalBytes, sent.groupTotalBytes);
}

TEST(NearbyFilePayloads, OfferFromAnOlderSenderDecodesAsOneLooseFile) {
  // Firmware that predates group transfers stops after the filename. Those bytes
  // still have to decode, as a single file bound for the card root.
  OfferPayload sent;
  sent.deviceName = "Lector";
  sent.fileName = "Book.epub";
  sent.fileSize = 2048;
  const std::vector<uint8_t> packet = encodeOffer(sent);

  const size_t legacyLength = 8 + 1 + sent.deviceName.size() + 1 + sent.fileName.size();
  ASSERT_LE(legacyLength, packet.size());

  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), legacyLength, got));
  EXPECT_EQ(got.fileName, sent.fileName);
  EXPECT_TRUE(got.folder.empty());
  EXPECT_EQ(got.groupIndex, 0);
  EXPECT_EQ(got.groupCount, 1);
  EXPECT_EQ(got.groupTotalBytes, sent.fileSize);
}

TEST(NearbyFilePayloads, OfferTruncatesAnOverlongFolder) {
  OfferPayload sent;
  sent.deviceName = "Lector";
  sent.fileName = "Face.cpfont";
  sent.fileSize = 10;
  sent.folder = std::string(MAX_FOLDER_BYTES + 40, 'd');

  const std::vector<uint8_t> packet = encodeOffer(sent);
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.folder.size(), MAX_FOLDER_BYTES);
}

TEST(NearbyFilePayloads, TheLargestOfferStillFitsOnePacket) {
  // Every field at its cap has to leave room for the packet header, or the offer
  // encodes here and then fails to go out, and the sender waits for an answer to
  // something it never sent.
  OfferPayload sent;
  sent.deviceName = std::string(MAX_NAME_BYTES, 'n');
  sent.fileName = std::string(MAX_OFFER_NAME_BYTES - 7, 'f') + ".cpfont";
  sent.fileSize = UINT64_MAX;
  sent.folder = std::string(MAX_FOLDER_BYTES, 'd');
  sent.groupIndex = 254;
  sent.groupCount = 255;
  sent.groupTotalBytes = UINT64_MAX;

  // Encoded into a packet-sized buffer, which is what the radio path hands it,
  // rather than the small one the other cases use.
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> buffer = {};
  size_t length = 0;
  ASSERT_TRUE(encodeOfferPayload(sent, buffer.data(), buffer.size(), length));
  EXPECT_LE(length + freeink::nearby::PACKET_HEADER_BYTES, freeink::nearby::MAX_PACKET_BYTES);
}

TEST(NearbyFilePayloads, CarriesEpubContentOffsetAndNativePageRecords) {
  for (const std::string name :
       {"Book.epub", "Book.EPUB", "Book.txt", "Book.md", "Book.xtc", "Book.xtch", "Book.pxc"}) {
    OfferPayload sent;
    sent.fileName = name;
    sent.fileSize = 1234;
    sent.position.bytes = {3, 0, 42, 0, 90, 0, 0x78, 0x56, 0x34, 0x12};
    sent.position.length = name == "Book.epub" || name == "Book.EPUB" ? 10 : 4;
    const auto packet = encodeOffer(sent);
    OfferPayload got;
    ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
    EXPECT_EQ(got.position.length, sent.position.length);
    EXPECT_TRUE(std::equal(sent.position.bytes.begin(), sent.position.bytes.begin() + sent.position.length,
                           got.position.bytes.begin()));
  }
}

TEST(NearbyFilePayloads, NewOfferPreservesEveryByteReadByLegacyReceivers) {
  OfferPayload sent;
  sent.deviceName = "Reader";
  sent.fileName = "Book.epub";
  sent.fileSize = 1234;
  const auto legacy = encodeOffer(sent);
  sent.position.length = 10;
  sent.position.bytes = {3, 0, 42, 0, 90, 0, 1, 2, 3, 4};
  const auto packet = encodeOffer(sent);
  ASSERT_EQ(packet.size(), legacy.size() + 15);
  // The old decoder returns after the group total and ignores trailing bytes.
  EXPECT_TRUE(std::equal(legacy.begin(), legacy.end(), packet.begin()));
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), legacy.size(), got));
  EXPECT_EQ(got.fileName, sent.fileName);
  EXPECT_EQ(got.fileSize, sent.fileSize);
  EXPECT_EQ(got.position.length, 0);
}

TEST(NearbyFilePayloads, OldOfferClearsPositionFromPreviousDecode) {
  OfferPayload sent;
  sent.fileName = "Book.epub";
  sent.fileSize = 1234;
  const auto packet = encodeOffer(sent);
  OfferPayload got;
  got.position.length = 10;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.position.length, 0);
  got.position.length = 10;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), 8 + 1 + 1 + sent.fileName.size(), got));
  EXPECT_EQ(got.position.length, 0);
}

TEST(NearbyFilePayloads, PositionRejectsEveryTruncatedTailAndOversizedLength) {
  OfferPayload sent;
  sent.fileName = "Book.epub";
  sent.fileSize = 1234;
  const size_t legacySize = encodeOffer(sent).size();
  sent.position.length = 10;
  auto packet = encodeOffer(sent);
  OfferPayload got;
  for (size_t length = legacySize + 1; length < packet.size(); ++length) {
    EXPECT_FALSE(decodeOfferPayload(packet.data(), length, got)) << length;
    EXPECT_EQ(got.position.length, 0);
  }
  packet[legacySize + 4] = 255;
  EXPECT_FALSE(decodeOfferPayload(packet.data(), packet.size(), got));
}

TEST(NearbyFilePayloads, PositionRejectsWrongFileTypeAndRecordSize) {
  OfferPayload sent;
  sent.fileName = "Book.epub";
  sent.fileSize = 1234;
  sent.position.length = 10;
  auto packet = encodeOffer(sent);
  // Same-length extension replacement exercises the receive trust boundary.
  const auto ext = std::search(packet.begin(), packet.end(), sent.fileName.begin(), sent.fileName.end());
  ASSERT_NE(ext, packet.end());
  std::copy_n("Face.font", 9, ext);
  OfferPayload got;
  EXPECT_FALSE(decodeOfferPayload(packet.data(), packet.size(), got));
  std::array<uint8_t, 256> buffer{};
  size_t length = 0;
  for (const std::string name : {"Book.txt", "Book.xtc", "Face.cpfont", "Picture.png", "Keys.cpcred"}) {
    sent.fileName = name;
    EXPECT_FALSE(encodeOfferPayload(sent, buffer.data(), buffer.size(), length));
  }
  sent.fileName = "Book.epub";
  for (const uint8_t invalid : {1, 3, 5, 7, 9, 11, 255}) {
    sent.position.length = invalid;
    EXPECT_FALSE(encodeOfferPayload(sent, buffer.data(), buffer.size(), length));
  }
}

TEST(NearbyFilePayloads, PositionTailChecksCapacityAndFitsTransport) {
  OfferPayload sent;
  sent.fileName = std::string(MAX_OFFER_NAME_BYTES - 5, 'b') + ".epub";
  sent.deviceName = std::string(MAX_NAME_BYTES, 'n');
  sent.fileSize = 1234;
  const size_t legacySize = encodeOffer(sent).size();
  sent.position.length = 10;
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> buffer{};
  size_t length = 0;
  EXPECT_FALSE(encodeOfferPayload(sent, buffer.data(), legacySize + 14, length));
  ASSERT_TRUE(encodeOfferPayload(sent, buffer.data(), buffer.size(), length));
  EXPECT_LE(length + freeink::nearby::PACKET_HEADER_BYTES, buffer.size());
}

TEST(NearbyFilePayloads, UnknownPositionVersionLeavesBookTransferUsable) {
  OfferPayload sent;
  sent.fileName = "Book.epub";
  const size_t legacySize = encodeOffer(sent).size();
  sent.position.length = 10;
  auto packet = encodeOffer(sent);
  packet[legacySize + 3] = 2;
  OfferPayload got;
  ASSERT_TRUE(decodeOfferPayload(packet.data(), packet.size(), got));
  EXPECT_EQ(got.position.length, 0);
  EXPECT_EQ(got.fileName, sent.fileName);
}
