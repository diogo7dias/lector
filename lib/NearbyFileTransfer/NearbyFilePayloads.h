#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Payloads carried inside the SDK's transfer packets.
 *
 * The SDK owns the envelope: magic, version, type, session id, sequence. What
 * each type carries inside is ours, and it is defined here rather than in the
 * activity so it can be round-tripped by host tests.
 *
 * Every decode runs on bytes that arrived over the air from a device this reader
 * has never met, so each one is bounds-checked and returns false rather than
 * trusting a length it was handed.
 */
namespace nearby_file {

/** Longest device name carried in an advertisement or an offer. */
constexpr size_t MAX_NAME_BYTES = 20;
/** Longest filename carried in an offer, matching what the rules will keep. */
constexpr size_t MAX_OFFER_NAME_BYTES = 180;

struct OfferPayload {
  std::string deviceName;
  std::string fileName;
  uint64_t fileSize = 0;
};

struct CompletePayload {
  uint32_t crc32 = 0;
  uint64_t totalBytes = 0;
};

/** Advertisement and discovery both carry just the sending device's name. */
bool encodeNamePayload(const std::string& deviceName, uint8_t* output, size_t capacity, size_t& outputLength);
bool decodeNamePayload(const uint8_t* data, size_t length, std::string& deviceName);

bool encodeOfferPayload(const OfferPayload& offer, uint8_t* output, size_t capacity, size_t& outputLength);
bool decodeOfferPayload(const uint8_t* data, size_t length, OfferPayload& offer);

bool encodeCompletePayload(const CompletePayload& complete, uint8_t* output, size_t capacity, size_t& outputLength);
bool decodeCompletePayload(const uint8_t* data, size_t length, CompletePayload& complete);

bool encodeResultPayload(bool success, uint8_t* output, size_t capacity, size_t& outputLength);
bool decodeResultPayload(const uint8_t* data, size_t length, bool& success);

}  // namespace nearby_file
