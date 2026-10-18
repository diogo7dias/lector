#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "TrustedPins.h"

using trusted_pins::findPin;
using trusted_pins::SHA256_SIZE;
using trusted_pins::withPin;

namespace {
void fill(uint8_t sha[SHA256_SIZE], uint8_t seed) {
  for (size_t i = 0; i < SHA256_SIZE; ++i) sha[i] = static_cast<uint8_t>(seed + i);
}
}  // namespace

TEST(TrustedPins, ASavedPinIsFoundAgainIgnoringHostCase) {
  uint8_t sha[SHA256_SIZE], got[SHA256_SIZE];
  fill(sha, 0xA0);
  const std::string text = withPin("", "Home.Local", sha);
  ASSERT_TRUE(findPin(text, "home.local", got));
  EXPECT_EQ(std::memcmp(got, sha, SHA256_SIZE), 0);
  EXPECT_FALSE(findPin(text, "other.local", got));
}

TEST(TrustedPins, RepinningAHostReplacesItsLine) {
  uint8_t a[SHA256_SIZE], b[SHA256_SIZE], got[SHA256_SIZE];
  fill(a, 1);
  fill(b, 2);
  std::string text = withPin("", "home.local", a);
  text = withPin(text, "nas.local", a);
  text = withPin(text, "home.local", b);
  ASSERT_TRUE(findPin(text, "home.local", got));
  EXPECT_EQ(std::memcmp(got, b, SHA256_SIZE), 0);
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 2);
}

TEST(TrustedPins, TheOldestPinGoesPastTheLimit) {
  uint8_t sha[SHA256_SIZE], got[SHA256_SIZE];
  fill(sha, 7);
  std::string text;
  for (size_t i = 0; i <= trusted_pins::MAX_PINS; ++i) text = withPin(text, "h" + std::to_string(i), sha);
  EXPECT_FALSE(findPin(text, "h0", got));
  EXPECT_TRUE(findPin(text, "h1", got));
  EXPECT_TRUE(findPin(text, "h" + std::to_string(trusted_pins::MAX_PINS), got));
  EXPECT_EQ(static_cast<size_t>(std::count(text.begin(), text.end(), '\n')), trusted_pins::MAX_PINS);
}

TEST(TrustedPins, AHandEditedFileWithJunkStillReads) {
  uint8_t got[SHA256_SIZE];
  const std::string text =
      "garbage\r\n"
      " leadingspace 00\n"
      "short abcd\n"
      "home.local 000102030405060708090a0b0c0d0e0f101112131415161718191A1B1C1D1E1F\r\n";
  ASSERT_TRUE(findPin(text, "home.local", got));
  EXPECT_EQ(got[0], 0x00);
  EXPECT_EQ(got[31], 0x1F);
  EXPECT_FALSE(findPin(text, "short", got));
}

TEST(TrustedPins, TheFingerprintIsTwelveBytesInGroups) {
  uint8_t sha[SHA256_SIZE];
  fill(sha, 0xC0);
  char out[40];
  trusted_pins::formatFingerprint(sha, out, sizeof(out));
  EXPECT_STREQ(out, "C0C1 C2C3 C4C5 C6C7 C8C9 CACB");
}
