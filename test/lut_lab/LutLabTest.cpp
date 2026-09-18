#include <BoardConfig.h>
#include <gtest/gtest.h>

#include <vector>

#include "driver/Uc8279X4Driver.h"
#include "lut/LutLabState.h"

namespace {
struct Write {
  uint8_t command;
  std::vector<uint8_t> bytes;
};
std::vector<Write> writes;
}  // namespace

// Link the REAL SDK driver against a recording bus: no copied LUT upload logic.
namespace freeink {
void EpdBus::cmd(uint8_t command) {
  writes.push_back({command, {}});
  writes.back().bytes.reserve(49);
}
void EpdBus::data(uint8_t byte) { writes.back().bytes.push_back(byte); }
void EpdBus::data(const uint8_t* bytes, uint16_t size) {
  writes.back().bytes.insert(writes.back().bytes.end(), bytes, bytes + size);
}
void EpdBus::reset(uint16_t) {}
void EpdBus::waitBusy(const char*) {}
void EpdBus::waitRefreshComplete(const char*) {}
}  // namespace freeink

TEST(LutLab, RealDriverWritesSelectedBankBeforeRefreshAndNullRestoresNativeStock) {
  writes.reserve(16);
  freeink::EpdBus bus;
  // Includes the SDK fallback for reserved/unknown LUT_VER values.
  for (uint8_t version : {0x02, 0x68, 0x69, 0x00}) {
    BoardConfig::ACTIVE.displayControllerVariant = version;
    freeink::Uc8279X4Driver driver;
    const auto& stock = version == 0x02 ? lutlab::CONTROL : lutlab::VARIANTS[2];
    // Null both before AND after each override catches sticky overrides.
    for (unsigned variant = 0; variant < lutlab::VARIANTS.size(); ++variant) {
      for (bool override : {false, true, false}) {
        const bool custom = override && variant != 0;
        const auto& expected = custom ? lutlab::VARIANTS[variant] : stock;
        writes.clear();
        driver.displayGray(bus, nullptr, false, custom ? expected.data() : nullptr, false);
        unsigned bank = 0;
        bool refreshed = false;
        for (const auto& write : writes) {
          if (write.command >= 0x20 && write.command <= 0x24) {
            EXPECT_FALSE(refreshed);
            ASSERT_LT(bank, 5u);
            EXPECT_EQ(write.command, 0x20 + bank);
            ASSERT_EQ(write.bytes.size(), 49u);
            for (unsigned byte = 0; byte < 49; ++byte) {
              EXPECT_EQ(write.bytes[byte], expected[bank * 49 + byte])
                  << "LUT_VER=" << unsigned(version) << " variant=" << variant << " table=" << bank << " byte=" << byte;
            }
            ++bank;
          }
          if (write.command == 0x12) {
            EXPECT_EQ(bank, 5u);
            refreshed = true;
          }
        }
        EXPECT_EQ(bank, 5u);
        EXPECT_TRUE(refreshed);
      }
    }
  }
}

TEST(LutLab, EveryProbeChangesOnlyGrayPhaseFramesTogetherAndPreservesPolarity) {
  constexpr unsigned frames[] = {2, 1, 3, 4, 5, 6, 7, 8};
  ASSERT_EQ(lutlab::VARIANTS.size(), std::size(frames));
  EXPECT_EQ(lutlab::VARIANTS[0], lutlab::CONTROL);
  for (unsigned variant = 0; variant < lutlab::VARIANTS.size(); ++variant) {
    for (unsigned offset = 0; offset < lutlab::CONTROL.size(); ++offset) {
      const unsigned expected =
          offset % 49 == 2 ? (lutlab::CONTROL[offset] & 0xF0) | frames[variant] : lutlab::CONTROL[offset];
      EXPECT_EQ(lutlab::VARIANTS[variant][offset], expected);
    }
  }
  EXPECT_EQ(lutlab::stepVariant(7, 1), 0);
  EXPECT_EQ(lutlab::stepVariant(0, -1), 7);
}

TEST(LutLab, SleepStateRoundTripsAndOldOrCorruptStateDefaultsSafely) {
  lutlab::State before;
  before.variant = 7;
  before.pinned = true;
  before.wallpaper = "/.sleep/photo.PXC";
  before.imageFailed = true;
  JsonDocument doc;
  before.toJson(doc.to<JsonObject>());
  std::string disk;
  serializeJson(doc, disk);
  JsonDocument read;
  ASSERT_FALSE(deserializeJson(read, disk));
  lutlab::State after;
  after.fromJson(read.as<JsonVariantConst>());
  EXPECT_EQ(after.variant, 7);
  EXPECT_TRUE(after.pinned);
  EXPECT_EQ(after.wallpaper, before.wallpaper);
  EXPECT_TRUE(after.imageFailed);

  for (const char* json :
       {"{}", R"({"waveform":"uc8279-aa","variant":-1,"pinned":true,"wallpaper":"/sleep/../book.bmp"})",
        R"({"waveform":"uc8279-aa","variant":256,"pinned":true,"wallpaper":"/other.bmp"})",
        R"({"waveform":"uc8279-aa","variant":8,"wallpaper":false})"}) {
    ASSERT_FALSE(deserializeJson(read, json));
    after.fromJson(read.as<JsonVariantConst>());
    EXPECT_EQ(after.variant, 0);
    EXPECT_FALSE(after.pinned);
    EXPECT_TRUE(after.wallpaper.empty());
  }
  ASSERT_FALSE(deserializeJson(read, R"({"variant":4,"pinned":true,"wallpaper":"/sleep/photo.bmp"})"));
  after.fromJson(read.as<JsonVariantConst>());
  EXPECT_EQ(after.variant, 0);  // Old SSD1677 index is never reinterpreted.
  EXPECT_TRUE(after.pinned);
  EXPECT_EQ(after.wallpaper, "/sleep/photo.bmp");

  EXPECT_TRUE(lutlab::validWallpaper("/sleep.bmp"));
  EXPECT_TRUE(lutlab::validWallpaper("/sleep/line art.bmp"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/.hidden.bmp"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/nested/image.pxc"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/a\\b.pxc"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/" + std::string(256, 'x') + ".bmp"));
}
