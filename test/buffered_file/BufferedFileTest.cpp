// Host tests for serialization::BufferedFileWriter/Reader (lib/Serialization/
// BufferedFile.h), the 4KB batching wrappers used by BookMetadataCache's build
// path. They lock three properties the build depends on:
//   1. Bytes round-trip identically to unbuffered HalFile writes (book.bin
//      layout is position-sensitive: LUT offsets are computed from position()).
//   2. position() reports the LOGICAL position, including still-buffered bytes.
//   3. seek() works both inside and outside the buffered window.

#include <BufferedFile.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

using serialization::BufferedFileReader;
using serialization::BufferedFileWriter;

TEST(BufferedFileWriter, RoundTripsMixedPodsAndStrings) {
  HalFile file;
  {
    BufferedFileWriter out(file, 16);  // small capacity to force mid-stream flushes
    serialization::writePod(out, static_cast<uint32_t>(0xDEADBEEF));
    serialization::writeString(out, "chapter1.xhtml");
    serialization::writePod(out, static_cast<int16_t>(-7));
    serialization::writeString(out, std::string(100, 'x'));  // longer than capacity
    EXPECT_TRUE(out.flush());
  }

  file.seek(0);
  uint32_t pod = 0;
  int16_t small = 0;
  std::string s1, s2;
  serialization::readPod(file, pod);
  serialization::readString(file, s1);
  serialization::readPod(file, small);
  serialization::readString(file, s2);
  EXPECT_EQ(pod, 0xDEADBEEF);
  EXPECT_EQ(s1, "chapter1.xhtml");
  EXPECT_EQ(small, -7);
  EXPECT_EQ(s2, std::string(100, 'x'));
}

TEST(BufferedFileWriter, PositionCountsBufferedBytes) {
  HalFile file;
  BufferedFileWriter out(file, 4096);
  serialization::writePod(out, static_cast<uint32_t>(1));
  serialization::writeString(out, "abc");
  // 4 (pod) + 4 (length prefix) + 3 (chars): still entirely in the buffer.
  EXPECT_EQ(out.position(), 11u);
  EXPECT_TRUE(out.flush());
  EXPECT_EQ(file.size(), 11u);
}

TEST(BufferedFileWriter, MatchesUnbufferedLayoutByteForByte) {
  HalFile buffered, plain;
  {
    BufferedFileWriter out(buffered, 8);
    for (int i = 0; i < 20; i++) {
      serialization::writeString(out, "entry" + std::to_string(i));
      serialization::writePod(out, static_cast<uint32_t>(i * 3));
    }
    EXPECT_TRUE(out.flush());
  }
  for (int i = 0; i < 20; i++) {
    serialization::writeString(plain, "entry" + std::to_string(i));
    serialization::writePod(plain, static_cast<uint32_t>(i * 3));
  }

  ASSERT_EQ(buffered.size(), plain.size());
  buffered.seek(0);
  plain.seek(0);
  std::string a(buffered.size(), '\0'), b(plain.size(), '\0');
  buffered.read(a.data(), a.size());
  plain.read(b.data(), b.size());
  EXPECT_EQ(a, b);
}

TEST(BufferedFileReader, ReadsAcrossBufferBoundaries) {
  HalFile file;
  for (int i = 0; i < 50; i++) {
    serialization::writeString(file, "item" + std::to_string(i));
    serialization::writePod(file, static_cast<uint32_t>(i));
  }

  file.seek(0);
  BufferedFileReader in(file, 16);  // forces many refills
  for (int i = 0; i < 50; i++) {
    std::string s;
    uint32_t v = 999;
    serialization::readString(in, s);
    serialization::readPod(in, v);
    EXPECT_EQ(s, "item" + std::to_string(i));
    EXPECT_EQ(v, static_cast<uint32_t>(i));
  }
}

TEST(BufferedFileReader, PositionMatchesUnbufferedSemantics) {
  HalFile file;
  serialization::writeString(file, "abcdef");
  serialization::writePod(file, static_cast<uint32_t>(42));

  file.seek(0);
  BufferedFileReader in(file, 4096);
  EXPECT_EQ(in.position(), 0u);
  std::string s;
  serialization::readString(in, s);
  EXPECT_EQ(in.position(), 10u);  // 4-byte length + 6 chars
  uint32_t v = 0;
  serialization::readPod(in, v);
  EXPECT_EQ(v, 42u);
  EXPECT_EQ(in.position(), 14u);
}

TEST(BufferedFileReader, SeekInsideAndOutsideWindow) {
  HalFile file;
  for (uint32_t i = 0; i < 100; i++) serialization::writePod(file, i);

  file.seek(0);
  BufferedFileReader in(file, 32);  // window covers 8 pods
  uint32_t v = 0;
  serialization::readPod(in, v);
  EXPECT_EQ(v, 0u);

  // Inside the buffered window.
  EXPECT_TRUE(in.seek(4 * 5));
  serialization::readPod(in, v);
  EXPECT_EQ(v, 5u);

  // Far outside the window.
  EXPECT_TRUE(in.seek(4 * 90));
  serialization::readPod(in, v);
  EXPECT_EQ(v, 90u);

  // Back to the start (rewind pattern used by buildBookBin's passes).
  EXPECT_TRUE(in.seek(0));
  serialization::readPod(in, v);
  EXPECT_EQ(v, 0u);
}

TEST(BufferedFileReader, OversizedLengthPrefixYieldsEmptyString) {
  HalFile file;
  const uint32_t bogus = 0x40000000;  // way past DEFAULT_MAX_STRING_BYTES
  file.write(reinterpret_cast<const uint8_t*>(&bogus), sizeof(bogus));

  file.seek(0);
  BufferedFileReader in(file, 64);
  std::string s = "sentinel";
  serialization::readString(in, s);
  EXPECT_TRUE(s.empty());
}

TEST(BufferedFileReader, ReadPastEofReturnsShort) {
  HalFile file;
  const uint8_t bytes[4] = {1, 2, 3, 4};
  file.write(bytes, sizeof(bytes));

  file.seek(0);
  BufferedFileReader in(file, 64);
  uint8_t dst[8] = {};
  EXPECT_EQ(in.read(dst, sizeof(dst)), 4u);
}

}  // namespace
