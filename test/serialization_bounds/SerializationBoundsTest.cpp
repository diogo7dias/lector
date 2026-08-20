// A cache file on the card is not trusted input.
//
// These formats are written by this firmware, but they are read back from a removable
// card, days later, possibly after a truncated write, possibly by a different firmware
// version. A string is stored as a 32-bit length followed by that many bytes, and the
// length used to go straight into std::string::resize(). A corrupt length therefore asked
// the allocator for an arbitrary size; on this target an unhandled std::bad_alloc is
// abort(), so a damaged byte in a TOC entry crashed the device outright. That is the
// failure these tests pin: a bad length must produce an empty string and a false return,
// never an allocation.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "SerializationLimits.h"

namespace {

// One length-prefixed string, exactly as writeString lays it out.
std::string encoded(const std::string& payload) {
  const uint32_t len = static_cast<uint32_t>(payload.size());
  std::string out(sizeof(len), '\0');
  std::memcpy(out.data(), &len, sizeof(len));
  out += payload;
  return out;
}

// A length prefix that does not match the bytes that follow.
std::string encodedWithLength(const uint32_t len, const std::string& payload) {
  std::string out(sizeof(len), '\0');
  std::memcpy(out.data(), &len, sizeof(len));
  out += payload;
  return out;
}

}  // namespace

TEST(SerializationBounds, AnOrdinaryStringRoundTrips) {
  std::istringstream in(encoded("Chapter 1: The Beginning"));
  std::string s;
  EXPECT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(s, "Chapter 1: The Beginning");
}

TEST(SerializationBounds, AnEmptyStringIsValid) {
  std::istringstream in(encoded(""));
  std::string s = "not cleared";
  EXPECT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

TEST(SerializationBounds, TheLargestAllowedStringIsStillAccepted) {
  const std::string payload(serialization::kMaxStringBytes, 'x');
  std::istringstream in(encoded(payload));
  std::string s;
  EXPECT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(s.size(), serialization::kMaxStringBytes);
}

// The crash. A length one byte over the cap must be refused outright rather than
// allocated and then found to be short.
TEST(SerializationBounds, ALengthOverTheCapIsRefusedWithoutAllocating) {
  std::istringstream in(encodedWithLength(serialization::kMaxStringBytes + 1, "abc"));
  std::string s = "not cleared";
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

TEST(SerializationBounds, AWildLengthIsRefused) {
  std::istringstream in(encodedWithLength(0xFFFFFFFFu, ""));
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

// Under the cap, but longer than what is actually left in the file: a truncated write.
TEST(SerializationBounds, ALengthPastTheEndOfTheDataIsRefused) {
  std::istringstream in(encodedWithLength(64, "only twelve!"));
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

// The length prefix itself is cut short. This used to leave `len` as uninitialized stack
// garbage, because readPod ignored the read result.
TEST(SerializationBounds, ATruncatedLengthPrefixIsRefused) {
  std::istringstream in(std::string("\x01\x02", 2));
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

TEST(SerializationBounds, ReadingPastTheEndOfAnExhaustedStreamIsRefused) {
  std::istringstream in("");
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_EQ(s, "");
}

TEST(SerializationBounds, ReadPodReportsAShortRead) {
  std::istringstream in(std::string("\x07", 1));
  uint32_t value = 0xDEADBEEF;
  EXPECT_FALSE(serialization::readPod(in, value));
  EXPECT_EQ(value, 0u);  // zeroed, never left as whatever was on the stack
}

TEST(SerializationBounds, ReadPodReadsACompleteValue) {
  const uint32_t written = 0x01020304;
  std::string bytes(sizeof(written), '\0');
  std::memcpy(bytes.data(), &written, sizeof(written));
  std::istringstream in(bytes);
  uint32_t value = 0;
  EXPECT_TRUE(serialization::readPod(in, value));
  EXPECT_EQ(value, written);
}

// Consecutive strings, the shape a TOC entry actually has (title, href, anchor).
TEST(SerializationBounds, ConsecutiveStringsReadInOrder) {
  std::istringstream in(encoded("Title") + encoded("text/ch1.xhtml") + encoded("#top"));
  std::string title, href, anchor;
  EXPECT_TRUE(serialization::readString(in, title));
  EXPECT_TRUE(serialization::readString(in, href));
  EXPECT_TRUE(serialization::readString(in, anchor));
  EXPECT_EQ(title, "Title");
  EXPECT_EQ(href, "text/ch1.xhtml");
  EXPECT_EQ(anchor, "#top");
}

// A refused read does not consume the payload it refused, so everything after it is being
// read at the wrong offset — whatever comes back is meaningless even when it parses. This
// is the reason readTocEntryFrom/readSpineEntryFrom abandon the whole entry on the first
// failure rather than reading on and keeping the fields that happen to succeed.
TEST(SerializationBounds, ARefusedReadLeavesTheStreamOutOfStep) {
  std::istringstream in(encodedWithLength(0x7FFFFFFFu, "") + encoded("href"));
  std::string title;
  ASSERT_FALSE(serialization::readString(in, title));

  // The next read succeeds, and its result is nonsense: it is the FOLLOWING field, not the
  // one the caller asked for. Only the false above tells the caller that.
  std::string href;
  EXPECT_TRUE(serialization::readString(in, href));
  EXPECT_EQ(href, "href");
}

TEST(SerializationBounds, PlausibilityIsBoundedByBothTheCapAndTheBytesLeft) {
  EXPECT_TRUE(serialization::stringLengthIsPlausible(0, 0));
  EXPECT_TRUE(serialization::stringLengthIsPlausible(10, 10));
  EXPECT_FALSE(serialization::stringLengthIsPlausible(11, 10));
  EXPECT_TRUE(serialization::stringLengthIsPlausible(serialization::kMaxStringBytes, 1u << 20));
  EXPECT_FALSE(serialization::stringLengthIsPlausible(serialization::kMaxStringBytes + 1, 1u << 20));
}
