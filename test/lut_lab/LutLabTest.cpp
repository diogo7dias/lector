#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <regex>

#include "lut/LutLabState.h"

TEST(LutLab, ControlMatchesShippingSdkAndEveryProbeChangesOnlyItsAdvertisedByte) {
  std::ifstream file(LUT_SOURCE);
  ASSERT_TRUE(file.good());
  std::string source((std::istreambuf_iterator<char>(file)), {});
  const auto start = source.find("lut_grayscale[] PROGMEM = {");
  ASSERT_NE(start, std::string::npos);
  source = source.substr(start, source.find("};", start) - start);
  source = std::regex_replace(source, std::regex("//[^\n]*"), "");
  const std::regex byte("0x[0-9A-Fa-f]+");
  size_t i = 0;
  for (auto it = std::sregex_iterator(source.begin(), source.end(), byte); it != std::sregex_iterator(); ++it, ++i) {
    ASSERT_LT(i, lutlab::CONTROL.size());
    EXPECT_EQ(lutlab::CONTROL[i], std::stoul(it->str(), nullptr, 16)) << i;
  }
  ASSERT_EQ(i, 112u);  // Includes voltage tail and reserved bytes, not just the 105-byte waveform.
  constexpr unsigned changed[] = {109, 109, 109, 109, 106, 106, 60, 61};
  constexpr unsigned values[] = {0x2F, 0x31, 0x2E, 0x32, 0x40, 0x42, 0x02, 0x02};
  EXPECT_EQ(lutlab::VARIANTS[0], lutlab::CONTROL);
  for (unsigned variant = 1; variant < lutlab::VARIANTS.size(); ++variant) {
    for (unsigned offset = 0; offset < lutlab::CONTROL.size(); ++offset) {
      EXPECT_EQ(lutlab::VARIANTS[variant][offset],
                offset == changed[variant - 1] ? values[variant - 1] : lutlab::CONTROL[offset]);
    }
  }
  EXPECT_EQ(lutlab::stepVariant(8, 1), 0);
  EXPECT_EQ(lutlab::stepVariant(0, -1), 8);
}

TEST(LutLab, SleepStateRoundTripsAndOldOrCorruptStateDefaultsSafely) {
  lutlab::State before;
  before.variant = 8;
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
  EXPECT_EQ(after.variant, 8);
  EXPECT_TRUE(after.pinned);
  EXPECT_EQ(after.wallpaper, before.wallpaper);
  EXPECT_TRUE(after.imageFailed);

  for (const char* json :
       {"{}", R"({"variant":-1,"pinned":true,"wallpaper":"/sleep/../book.bmp"})",
        R"({"variant":256,"pinned":true,"wallpaper":"/other.bmp"})", R"({"variant":9,"wallpaper":false})"}) {
    ASSERT_FALSE(deserializeJson(read, json));
    after.fromJson(read.as<JsonVariantConst>());
    EXPECT_EQ(after.variant, 0);
    EXPECT_FALSE(after.pinned);
    EXPECT_TRUE(after.wallpaper.empty());
  }
  EXPECT_TRUE(lutlab::validWallpaper("/sleep.bmp"));
  EXPECT_TRUE(lutlab::validWallpaper("/sleep/line art.bmp"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/.hidden.bmp"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/nested/image.pxc"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/a\\b.pxc"));
  EXPECT_FALSE(lutlab::validWallpaper("/sleep/" + std::string(256, 'x') + ".bmp"));
}
