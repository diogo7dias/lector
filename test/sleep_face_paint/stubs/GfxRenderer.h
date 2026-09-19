#pragma once

#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <vector>

// Records renderer calls and their uniform target; it does not simulate waveforms.
class GfxRenderer {
 public:
  int width = 528;
  int height = 792;
  uint8_t color = 0;
  std::vector<uint8_t> targets;

  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void clearScreen(uint8_t value = 0xFF) { color = value; }
  void fillRect(int x, int y, int w, int h, bool black) {
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(w, width);
    EXPECT_EQ(h, height);
    color = black ? 0 : 0xFF;
  }
  void displayBuffer(HalDisplay::RefreshMode mode) {
    EXPECT_EQ(mode, HalDisplay::FULL_REFRESH);
    targets.push_back(color);
  }
};
