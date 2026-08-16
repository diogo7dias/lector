#include <gtest/gtest.h>

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

  OfferPayload got;
  for (size_t length = 0; length < packet.size(); length++) {
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
