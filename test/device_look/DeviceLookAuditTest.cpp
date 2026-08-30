// Source audit: every FreeInkUI control that only a finger can drive keeps a
// keys-only shape beside it.
//
// Only the X4 Pro has a touch panel. The slider row's capsule and its two step
// buttons, and the grid cell drawn as a button, exist because they are tapped;
// on the X3 and the X4 they would be decoration over a value the buttons were
// driving anyway, and those boards are meant to look the way they always did.
// The guard is `mappedInput.hasTouch()`, not a device macro: the `default`
// environment builds one binary for both keys-only boards, so a compile-time
// split could not tell them apart from the X4 Pro anyway.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TOUCH_SHAPE_SOURCES
#error "TOUCH_SHAPE_SOURCES must be defined by the build system"
#endif

namespace {

std::vector<std::string> sourcePaths() {
  std::vector<std::string> paths;
  std::stringstream all(TOUCH_SHAPE_SOURCES);
  std::string path;
  while (std::getline(all, path, '|')) {
    if (!path.empty()) paths.push_back(path);
  }
  return paths;
}

std::string readSource(const std::string& path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

}  // namespace

TEST(DeviceLookAudit, EveryTouchOnlyShapeAsksWhetherTheBoardHasTouch) {
  for (const std::string& path : sourcePaths()) {
    const std::string source = readSource(path);
    EXPECT_TRUE(contains(source, "mappedInput.hasTouch()")) << path << " draws a touch-only shape unconditionally";
  }
}

TEST(DeviceLookAudit, EverySliderSurfaceHasAKeysOnlyBar) {
  for (const std::string& path : sourcePaths()) {
    const std::string source = readSource(path);
    if (!contains(source, "fui::sliderRow(")) continue;
    EXPECT_TRUE(contains(source, "plain_slider_band::draw"))
        << path << " has a slider row with no bar for the keys-only boards";
  }
}
