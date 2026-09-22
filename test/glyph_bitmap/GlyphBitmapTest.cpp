#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "lib/GfxRenderer/GlyphBitmap.h"

// Pins glyphBitmap::draw, as driven by renderCharImpl/drawGlyphBitmap, to the
// per-pixel drawPixel loop it replaced: every orientation, both text
// rotations, 1bpp and 2bpp, all three planes, Paperback Look smear, strip
// bands and panel-edge clipping must give byte-identical buffers.
namespace {
constexpr int PANEL_WIDTH = 40;
constexpr int PANEL_HEIGHT = 32;
constexpr int STRIDE = PANEL_WIDTH / 8;
constexpr int GUARD = 16;
constexpr int ASCENDER = 9;

enum Mode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

// GfxRenderer::Orientation order: Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise.
void rotateCoordinates(int orientation, int x, int y, int* phyX, int* phyY) {
  switch (orientation) {
    case 0:
      *phyX = y;
      *phyY = PANEL_HEIGHT - 1 - x;
      break;
    case 1:
      *phyX = PANEL_WIDTH - 1 - x;
      *phyY = PANEL_HEIGHT - 1 - y;
      break;
    case 2:
      *phyX = PANEL_WIDTH - 1 - y;
      *phyY = x;
      break;
    default:
      *phyX = x;
      *phyY = y;
  }
}

struct Case {
  int orientation;
  bool rotated;
  bool twoBit;
  Mode mode;
  bool state;
  bool paperback;
  int width;
  int height;
  int cursorX;
  int cursorY;
  int left;
  int top;
  int stripY0;  // stripRows == 0 means no strip target
  int stripRows;
};

// The pre-port GfxRenderer::drawPixel.
void drawPixel(const Case& c, uint8_t* buffer, int x, int y, bool state) {
  int phyX, phyY;
  rotateCoordinates(c.orientation, x, y, &phyX, &phyY);
  if (phyX < 0 || phyX >= PANEL_WIDTH || phyY < 0 || phyY >= PANEL_HEIGHT) return;
  int rowY = phyY;
  if (c.stripRows) {
    if (phyY < c.stripY0 || phyY >= c.stripY0 + c.stripRows) return;
    rowY = phyY - c.stripY0;
  }
  const int byteIndex = rowY * STRIDE + phyX / 8;
  const uint8_t bitPosition = 7 - (phyX % 8);
  if (state)
    buffer[byteIndex] &= ~(1 << bitPosition);
  else
    buffer[byteIndex] |= 1 << bitPosition;
}

// The pre-port renderCharImpl pixel loop, after strip culling.
void renderOld(const Case& c, const uint8_t* bitmap, uint8_t* buffer) {
  const bool pixelState = c.state;
  int outerBase, innerBase;
  if (c.rotated) {
    outerBase = c.cursorX + ASCENDER - c.top;
    innerBase = c.cursorY - c.left;
  } else {
    outerBase = c.cursorY - c.top;
    innerBase = c.cursorX + c.left;
  }
  int pixelPosition = 0;
  for (int glyphY = 0; glyphY < c.height; glyphY++) {
    const int outerCoord = outerBase + glyphY;
    for (int glyphX = 0; glyphX < c.width; glyphX++, pixelPosition++) {
      const int screenX = c.rotated ? outerCoord : innerBase + glyphX;
      const int screenY = c.rotated ? innerBase - glyphX : outerCoord;
      if (c.twoBit) {
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
        const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);
        if (c.mode == BW && bmpVal < 3) {
          drawPixel(c, buffer, screenX, screenY, pixelState);
          if (c.paperback) {
            drawPixel(c, buffer, screenX + 1, screenY, pixelState);
            drawPixel(c, buffer, screenX, screenY + 1, pixelState);
          }
        } else if (c.mode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
          drawPixel(c, buffer, screenX, screenY, false);
        } else if (c.mode == GRAYSCALE_LSB && bmpVal == 1) {
          drawPixel(c, buffer, screenX, screenY, false);
        }
      } else {
        const uint8_t byte = bitmap[pixelPosition >> 3];
        const uint8_t bit_index = 7 - (pixelPosition & 7);
        if ((byte >> bit_index) & 1) {
          drawPixel(c, buffer, screenX, screenY, pixelState);
          if (c.mode == BW && c.paperback) {
            drawPixel(c, buffer, screenX + 1, screenY, pixelState);
            drawPixel(c, buffer, screenX, screenY + 1, pixelState);
          }
        }
      }
    }
  }
}

// Mirrors GfxRenderer::drawGlyphBitmap.
void drawGlyphBitmap(const Case& c, const uint8_t* bitmap, uint8_t* buffer, const glyphBitmap::Frame& frame) {
  glyphBitmap::Target target{
      buffer, PANEL_WIDTH, STRIDE, c.stripRows ? c.stripY0 : 0, c.stripRows ? c.stripRows : PANEL_HEIGHT, {}};
  glyphBitmap::Frame& physical = target.frame;
  rotateCoordinates(c.orientation, frame.x, frame.y, &physical.x, &physical.y);
  int nextX, nextY;
  rotateCoordinates(c.orientation, frame.x + frame.dxX, frame.y + frame.dxY, &nextX, &nextY);
  physical.dxX = nextX - physical.x;
  physical.dxY = nextY - physical.y;
  rotateCoordinates(c.orientation, frame.x + frame.dyX, frame.y + frame.dyY, &nextX, &nextY);
  physical.dyX = nextX - physical.x;
  physical.dyY = nextY - physical.y;
  const glyphBitmap::Plane plane = c.mode == BW              ? glyphBitmap::Plane::BW
                                   : c.mode == GRAYSCALE_MSB ? glyphBitmap::Plane::GrayMSB
                                                             : glyphBitmap::Plane::GrayLSB;
  glyphBitmap::draw(bitmap, c.width, c.height, c.twoBit, plane, c.state, target, {0, 0, c.width, c.height});
}

// Mirrors the post-port renderCharImpl tail.
void renderNew(const Case& c, const uint8_t* bitmap, uint8_t* buffer) {
  glyphBitmap::Frame frame;
  if (c.rotated) {
    frame = {c.cursorX + ASCENDER - c.top, c.cursorY - c.left, 0, -1, 1, 0};
  } else {
    frame = {c.cursorX + c.left, c.cursorY - c.top, 1, 0, 0, 1};
  }
  drawGlyphBitmap(c, bitmap, buffer, frame);
  if (c.mode == BW && c.paperback) {
    frame.x++;
    drawGlyphBitmap(c, bitmap, buffer, frame);
    frame.x--;
    frame.y++;
    drawGlyphBitmap(c, bitmap, buffer, frame);
  }
}

void compare(const Case& c, int seed) {
  SCOPED_TRACE(::testing::Message() << "o=" << c.orientation << " rot=" << c.rotated << " 2bit=" << c.twoBit
                                    << " mode=" << c.mode << " state=" << c.state << " pb=" << c.paperback
                                    << " size=" << c.width << 'x' << c.height << " cursor=" << c.cursorX << ','
                                    << c.cursorY << " strip=" << c.stripY0 << ',' << c.stripRows);
  std::vector<uint8_t> bitmap((c.width * c.height * (c.twoBit ? 2 : 1) + 7) / 8);
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = static_cast<uint8_t>(i * 73 + seed);
  // Nonuniform background plus guards catch transparent-pixel and out-of-band writes.
  std::vector<uint8_t> expected((c.stripRows ? c.stripRows : PANEL_HEIGHT) * STRIDE + 2 * GUARD);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  renderOld(c, bitmap.data(), expected.data() + GUARD);
  renderNew(c, bitmap.data(), actual.data() + GUARD);
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(GlyphBitmap, MatchesPerPixelRenderCharImpl) {
  const std::pair<int, int> cursors[] = {{-12, -6}, {0, 0}, {5, 14}, {17, 22}, {30, 38}, {39, 31}};
  const std::pair<int, int> strips[] = {{0, 0}, {0, 32}, {0, 7}, {7, 11}, {28, 4}};
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool rotated : {false, true})
      for (bool twoBit : {false, true})
        for (Mode mode : {BW, GRAYSCALE_LSB, GRAYSCALE_MSB})
          for (bool state : {false, true})
            for (bool paperback : {false, true})
              for (int width : {1, 3, 7, 8, 16, 31})
                for (int height : {1, 13})
                  for (const auto& [cx, cy] : cursors)
                    for (const auto& [y0, rows] : strips)
                      for (int seed : {0x1b, 0xc4})
                        compare({orientation, rotated, twoBit, mode, state, paperback, width, height, cx, cy, -2, 11,
                                 y0, rows},
                                seed);
}

TEST(GlyphBitmap, EmptyAndOffPanelGlyphsDoNotWrite) {
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool rotated : {false, true}) {
      compare({orientation, rotated, true, BW, true, true, 0, 0, 0, 0, 0, 0, 0, 0}, 0x1b);
      compare({orientation, rotated, true, BW, true, true, 11, 9, 100, 100, 0, 0, 0, 0}, 0x1b);
      compare({orientation, rotated, false, BW, true, true, 11, 9, -100, -100, 0, 0, 0, 0}, 0x1b);
    }
}
