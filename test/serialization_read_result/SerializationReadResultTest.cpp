// Host tests for serialization::readStringResult classification.
//
// The BookMetadataCache self-heal (delete book.bin, rebuild next open) fires
// only on StringReadResult::Corrupt. These tests lock the Corrupt vs Ok
// classification so on-disk damage is always detected and healthy data never
// misclassified. The LowMemory branch depends on the ESP32 heap and is
// compiled out on the host (hasStringAllocationHeadroom returns true).

#include <gtest/gtest.h>

#include <Serialization.h>

#include <cstring>
#include <string>

namespace {

HalFile fileWith(const void* data, size_t len) {
  HalFile f;
  f.write(static_cast<const uint8_t*>(data), len);
  f.seek(0);
  return f;
}

HalFile fileWithString(const std::string& s) {
  HalFile f;
  serialization::writeString(f, s);
  f.seek(0);
  return f;
}

TEST(SerializationReadResult, ValidStringIsOk) {
  auto f = fileWithString("chapter1.xhtml");
  std::string out;
  EXPECT_EQ(serialization::readStringResult(f, out), serialization::StringReadResult::Ok);
  EXPECT_EQ(out, "chapter1.xhtml");
}

TEST(SerializationReadResult, EmptyStringIsOk) {
  auto f = fileWithString("");
  std::string out = "sentinel";
  EXPECT_EQ(serialization::readStringResult(f, out), serialization::StringReadResult::Ok);
  EXPECT_TRUE(out.empty());
}

TEST(SerializationReadResult, TruncatedLengthPrefixIsCorrupt) {
  const uint8_t partial[] = {0x05, 0x00};  // only 2 of the 4 length bytes
  auto f = fileWith(partial, sizeof(partial));
  std::string out;
  EXPECT_EQ(serialization::readStringResult(f, out), serialization::StringReadResult::Corrupt);
  EXPECT_TRUE(out.empty());
}

TEST(SerializationReadResult, LengthPastEndOfFileIsCorrupt) {
  // Length says 100 bytes but only 3 follow.
  const uint8_t data[] = {100, 0, 0, 0, 'a', 'b', 'c'};
  auto f = fileWith(data, sizeof(data));
  std::string out;
  EXPECT_EQ(serialization::readStringResult(f, out), serialization::StringReadResult::Corrupt);
  EXPECT_TRUE(out.empty());
}

TEST(SerializationReadResult, GarbageHugeLengthIsCorrupt) {
  const uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 'a', 'b', 'c'};
  auto f = fileWith(data, sizeof(data));
  std::string out;
  EXPECT_EQ(serialization::readStringResult(f, out), serialization::StringReadResult::Corrupt);
  EXPECT_TRUE(out.empty());
}

TEST(SerializationReadResult, ReadStringMatchesResultClassification) {
  auto good = fileWithString("x");
  std::string out;
  EXPECT_TRUE(serialization::readString(good, out));

  const uint8_t bad[] = {100, 0, 0, 0, 'a'};
  auto corrupt = fileWith(bad, sizeof(bad));
  EXPECT_FALSE(serialization::readString(corrupt, out));
}

}  // namespace
