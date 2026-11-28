#pragma once
// The renderer state DirectPixelWriter reads, over a write target the harness owns. The
// harness sizes that target exactly (whole frame, or one grayscale strip) so a stray write
// past it trips ASan.
#include <cstdint>

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  uint8_t* target = nullptr;
  int originY = 0;
  int rows = 0;
  RenderMode mode = BW;
  Orientation orientation = Portrait;
  uint16_t panelWidth = 800;
  uint16_t panelHeight = 480;

  bool imagesDitheredInBw() const { return false; }
  bool isDisplayInverted() const { return false; }
  bool landscape() const { return orientation == LandscapeClockwise || orientation == LandscapeCounterClockwise; }
  int getScreenWidth() const { return landscape() ? panelWidth : panelHeight; }
  int getScreenHeight() const { return landscape() ? panelHeight : panelWidth; }
  uint8_t* getWriteTarget() const { return target; }
  int getWriteOriginY() const { return originY; }
  int getWriteRows() const { return rows; }
  RenderMode getRenderMode() const { return mode; }
  Orientation getOrientation() const { return orientation; }
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  uint16_t getDisplayWidthBytes() const { return panelWidth / 8; }
};
