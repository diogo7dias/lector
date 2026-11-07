#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "lib/Serialization/CredentialIntegrity.h"

namespace {

// A flipped bit in the stored base64 decodes to plaintext of the same length, so the
// length check alone passes it; only the checksum can reject it.
TEST(CredentialIntegrity, RejectsSameLengthCorruption) {
  const std::string plaintext = "password";
  const uint32_t expectedCrc32 = credential_integrity::crc32(plaintext);

  std::string corrupted = plaintext;
  corrupted[1] ^= 0x01;

  EXPECT_TRUE(credential_integrity::validate(plaintext, plaintext.size(), expectedCrc32));
  EXPECT_FALSE(credential_integrity::validate(corrupted, plaintext.size(), expectedCrc32));
}

}  // namespace
