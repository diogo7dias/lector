#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "FlashWriteVerify.h"

namespace {

using firmware_flash::Result;

// Deterministic pseudo-image bytes. Content is irrelevant to the write path;
// only that source and flash end up byte-identical.
std::vector<uint8_t> makeImage(size_t size) {
  std::vector<uint8_t> v(size);
  for (size_t i = 0; i < size; i++) v[i] = static_cast<uint8_t>((i * 31u + (i >> 8)) & 0xFF);
  return v;
}

class FakeSource : public firmware_flash::ByteSource {
 public:
  explicit FakeSource(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}
  size_t size() const override { return data.size(); }
  bool rewind() override {
    pos = 0;
    return true;
  }
  int read(uint8_t* dst, size_t len) override {
    const size_t got = std::min(len, data.size() - pos);
    std::memcpy(dst, data.data() + pos, got);
    pos += got;
    return static_cast<int>(got);
  }
  std::vector<uint8_t> data;
  size_t pos = 0;
};

// NOR-flash semantics: erased bytes are 0xFF, a write can only clear bits.
// Writing into a region that was never erased therefore corrupts silently,
// which is exactly the failure the readback pass has to catch.
class FakeFlash : public firmware_flash::FlashTarget {
 public:
  explicit FakeFlash(size_t bytes) : cells(bytes, 0x00) {}
  size_t size() const override { return cells.size(); }
  bool erase(size_t offset, size_t len) override {
    if (offset % 4096 != 0 || len % 4096 != 0) return false;  // hardware rejects unaligned erases
    if (offset + len > cells.size()) return false;
    eraseCalls++;
    std::fill(cells.begin() + offset, cells.begin() + offset + len, 0xFF);
    return true;
  }
  bool write(size_t offset, const uint8_t* src, size_t len) override {
    if (offset + len > cells.size()) return false;
    if (failWriteAt && offset >= *failWriteAt) return false;
    for (size_t i = 0; i < len; i++) cells[offset + i] &= src[i];
    if (corruptAt && *corruptAt >= offset && *corruptAt < offset + len) cells[*corruptAt] ^= 0x01;
    return true;
  }
  bool read(size_t offset, uint8_t* dst, size_t len) override {
    if (offset + len > cells.size()) return false;
    std::memcpy(dst, cells.data() + offset, len);
    return true;
  }
  std::vector<uint8_t> cells;
  std::optional<size_t> corruptAt;
  std::optional<size_t> failWriteAt;
  int eraseCalls = 0;
};

constexpr size_t kPartition = 512 * 1024;

TEST(FirmwareFlashWrite, WritesImageAndVerifiesReadback) {
  FakeSource src(makeImage(300 * 1024 + 1234));  // deliberately not a sector multiple
  FakeFlash flash(kPartition);

  ASSERT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::OK);
  EXPECT_EQ(firmware_flash::verifyImage(src, flash), Result::OK);
  EXPECT_TRUE(std::equal(src.data.begin(), src.data.end(), flash.cells.begin()));
}

TEST(FirmwareFlashWrite, VerifyCatchesASingleFlippedBit) {
  FakeSource src(makeImage(200 * 1024));
  FakeFlash flash(kPartition);
  flash.corruptAt = 128 * 1024 + 7;  // one bit lost mid-image, as a bad write would

  ASSERT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::OK);
  EXPECT_EQ(firmware_flash::verifyImage(src, flash), Result::VERIFY_FAIL);
}

TEST(FirmwareFlashWrite, VerifyCatchesATruncatedWrite) {
  FakeSource src(makeImage(200 * 1024));
  FakeFlash flash(kPartition);
  // Everything past this offset stays erased: the image on flash is short.
  ASSERT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::OK);
  std::fill(flash.cells.begin() + 150 * 1024, flash.cells.end(), 0xFF);

  EXPECT_EQ(firmware_flash::verifyImage(src, flash), Result::VERIFY_FAIL);
}

TEST(FirmwareFlashWrite, EveryWrittenByteWasErasedFirst) {
  FakeSource src(makeImage(300 * 1024 + 1234));
  FakeFlash flash(kPartition);
  std::fill(flash.cells.begin(), flash.cells.end(), 0x00);  // worst case: all bits already cleared

  ASSERT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::OK);
  // Without a preceding erase the AND-semantics fake would leave zeros behind,
  // so an exact match proves the erase ran ahead of every write.
  EXPECT_TRUE(std::equal(src.data.begin(), src.data.end(), flash.cells.begin()));
}

TEST(FirmwareFlashWrite, ReportsWriteFailure) {
  FakeSource src(makeImage(200 * 1024));
  FakeFlash flash(kPartition);
  flash.failWriteAt = 64 * 1024;

  EXPECT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::WRITE_FAIL);
}

TEST(FirmwareFlashWrite, RefusesAnImageLargerThanThePartition) {
  FakeSource src(makeImage(kPartition + 4096));
  FakeFlash flash(kPartition);

  EXPECT_EQ(firmware_flash::writeImage(src, flash, nullptr, nullptr), Result::TOO_LARGE);
}

TEST(FirmwareFlashWrite, ProgressReachesTotal) {
  FakeSource src(makeImage(200 * 1024 + 5));
  FakeFlash flash(kPartition);
  struct Seen {
    size_t written = 0;
    size_t total = 0;
    int calls = 0;
  } seen;
  auto cb = +[](size_t written, size_t total, void* ctx) {
    auto* s = static_cast<Seen*>(ctx);
    s->written = written;
    s->total = total;
    s->calls++;
  };

  ASSERT_EQ(firmware_flash::writeImage(src, flash, cb, &seen), Result::OK);
  EXPECT_EQ(seen.written, src.size());
  EXPECT_EQ(seen.total, src.size());
  EXPECT_GT(seen.calls, 1);
}

// StreamWriter: the same erase-ahead write, but push-mode, for an image that
// arrives from the network in chunks the caller does not control and cannot
// rewind.

TEST(FirmwareStreamWriter, WritesAChunkedImageIdenticallyToWriteImage) {
  const std::vector<uint8_t> image = makeImage(200 * 1024 + 77);
  FakeFlash flash(kPartition);
  std::fill(flash.cells.begin(), flash.cells.end(), 0x00);  // worst case: all bits already cleared
  firmware_flash::StreamWriter writer(flash);

  // Chunk sizes a TLS transport actually hands over: unaligned and varying.
  size_t pos = 0;
  for (size_t chunk = 1000; pos < image.size(); chunk = chunk == 1000 ? 2048 : 1000) {
    const size_t take = std::min(chunk, image.size() - pos);
    ASSERT_EQ(writer.write(image.data() + pos, take), Result::OK);
    pos += take;
  }

  EXPECT_EQ(writer.written(), image.size());
  EXPECT_TRUE(std::equal(image.begin(), image.end(), flash.cells.begin()));
}

TEST(FirmwareStreamWriter, RestartRewritesFromZeroWithoutStackingTwoCopies) {
  const std::vector<uint8_t> image = makeImage(100 * 1024);
  FakeFlash flash(kPartition);
  firmware_flash::StreamWriter writer(flash);

  // A server that ignores our Range header replays the body from byte 0.
  ASSERT_EQ(writer.write(image.data(), 70 * 1024), Result::OK);
  writer.restart();
  ASSERT_EQ(writer.write(image.data(), image.size()), Result::OK);

  EXPECT_EQ(writer.written(), image.size());
  EXPECT_TRUE(std::equal(image.begin(), image.end(), flash.cells.begin()));
}

TEST(FirmwareStreamWriter, RefusesToRunPastThePartition) {
  FakeFlash flash(64 * 1024);
  firmware_flash::StreamWriter writer(flash);
  const std::vector<uint8_t> image = makeImage(64 * 1024 + 1);

  EXPECT_EQ(writer.write(image.data(), image.size()), Result::TOO_LARGE);
}

TEST(FirmwareStreamWriter, ReportsWriteFailure) {
  FakeFlash flash(kPartition);
  flash.failWriteAt = 32 * 1024;
  firmware_flash::StreamWriter writer(flash);
  const std::vector<uint8_t> image = makeImage(100 * 1024);

  EXPECT_EQ(writer.write(image.data(), image.size()), Result::WRITE_FAIL);
}

}  // namespace

#include "FirmwareSwitchAuditLine.h"

namespace {

TEST(FirmwareSwitchAudit, LineNamesBothSlotsAndTheImageSize) {
  const std::string line = firmware_flash::formatSwitchFailedLine("lector 0.28.0", 0x650000, 0x010000, 5544112);

  EXPECT_NE(line.find("lector 0.28.0"), std::string::npos);
  EXPECT_NE(line.find("0x650000"), std::string::npos);
  EXPECT_NE(line.find("0x010000"), std::string::npos);
  EXPECT_NE(line.find("5544112"), std::string::npos);
  EXPECT_EQ(line.back(), '\n');  // the log is appended to, one attempt per line
}

TEST(FirmwareSwitchAudit, MissingVersionDoesNotProduceGarbage) {
  const std::string line = firmware_flash::formatSwitchFailedLine(nullptr, 0x650000, 0x010000, 1);

  EXPECT_EQ(line.rfind("?: ", 0), 0u);
}

}  // namespace

#include "ChipIdNames.h"

namespace {

TEST(ChipIdNames, NamesKnownChipsAccurately) {
  EXPECT_STREQ(firmware_flash::chipName(0x0005), "ESP32-C3");
  EXPECT_STREQ(firmware_flash::chipName(0x0009), "ESP32-S3");
  EXPECT_STREQ(firmware_flash::chipName(0x0000), "ESP32");
  EXPECT_STREQ(firmware_flash::chipName(0x0002), "ESP32-S2");
  EXPECT_STREQ(firmware_flash::chipName(0x000C), "ESP32-C2");
  EXPECT_STREQ(firmware_flash::chipName(0x000D), "ESP32-C6");
  EXPECT_STREQ(firmware_flash::chipName(0x0010), "ESP32-H2");
  EXPECT_STREQ(firmware_flash::chipName(0x1234), "Unknown");
}

}  // namespace
