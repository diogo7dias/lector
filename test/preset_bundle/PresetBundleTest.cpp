#include <gtest/gtest.h>

#include "util/PresetBundle.h"

TEST(PresetBundle, RecognisesBundleFilenamesCaseInsensitively) {
  EXPECT_TRUE(preset_bundle::isBundleFilename("themes.cpthemes"));
  EXPECT_TRUE(preset_bundle::isBundleFilename("MyThemes.CPTHEMES"));
  EXPECT_TRUE(preset_bundle::isBundleFilename("/.crosspoint/themes.cpthemes"));
  EXPECT_FALSE(preset_bundle::isBundleFilename(".cpthemes"));
  EXPECT_FALSE(preset_bundle::isBundleFilename("cpthemes"));
  EXPECT_FALSE(preset_bundle::isBundleFilename("themes.json"));
  EXPECT_FALSE(preset_bundle::isBundleFilename(""));
}
