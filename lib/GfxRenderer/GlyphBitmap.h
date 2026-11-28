#pragma once

#include <algorithm>
#include <cstdint>

// Glyph rasterizer that resolves orientation, clipping and framebuffer
// addressing once per glyph instead of once per pixel. It has no dependency
// on GfxRenderer so it can be unit-tested on the host.
namespace glyphBitmap {

// Framebuffer plane a glyph is painted into.
enum class Plane : uint8_t { BW, GrayLSB, GrayMSB };

// Position of glyph pixel (0, 0) plus the step for one move along the glyph's
// x and y axes, all in one coordinate space. The axes must describe an
// orthogonal unit rotation: each is +/-1 on exactly one component.
struct Frame {
  int x;
  int y;
  int dxX;
  int dxY;
  int dyX;
  int dyY;
};

// Where a glyph lands in physical framebuffer space.
struct Target {
  uint8_t* buffer;  // First byte of row originY (framebuffer or strip scratch)
  int width;        // Physical panel width in pixels
  int stride;       // Bytes per physical row
  int originY;      // First physical row held by buffer
  int rows;         // Number of physical rows held by buffer
  Frame frame;      // Physical placement of the glyph
};

// Half-open rectangle in glyph-local pixel coordinates.
struct Clip {
  int left;
  int top;
  int right;
  int bottom;
};

// Narrow [start, end) on one glyph axis so that base + i * step stays within
// [lower, upper). step is +1 or -1 because the transform is orthogonal.
inline void clipAxis(int base, int step, int lower, int upper, int& start, int& end) {
  if (step > 0) {
    start = std::max(start, lower - base);
    end = std::min(end, upper - base);
  } else {
    start = std::max(start, base - upper + 1);
    end = std::min(end, base - lower + 1);
  }
}

// Narrow a glyph-local clip so every kept pixel maps inside the half-open
// rectangle [left, right) x [top, bottom) of the space frame is expressed in.
// Which glyph axis runs along that space's x depends on the rotation:
// dxX != 0 means glyph x does. Runs twice per glyph, so it stays inline
// under -Os.
__attribute__((always_inline)) inline void clipToRect(const Frame& frame, int left, int top, int right, int bottom,
                                                      Clip& clip) {
  if (frame.dxX != 0) {
    clipAxis(frame.x, frame.dxX, left, right, clip.left, clip.right);
    clipAxis(frame.y, frame.dyY, top, bottom, clip.top, clip.bottom);
  } else {
    clipAxis(frame.x, frame.dyX, left, right, clip.top, clip.bottom);
    clipAxis(frame.y, frame.dxY, top, bottom, clip.left, clip.right);
  }
}

// Keep pixel writes inline when decoding a group of four pixels.
__attribute__((always_inline)) inline void paint(uint8_t* buffer, int destination, uint8_t ink, uint8_t levels,
                                                 bool clearBits) {
  if ((levels & (1u << ink)) == 0) return;
  const uint8_t mask = 0x80u >> (destination & 7);
  if (clearBits)
    buffer[destination >> 3] &= static_cast<uint8_t>(~mask);
  else
    buffer[destination >> 3] |= mask;
}

// BW 2-bit pixel level mask (bit n = paint 2-bit value n):
//   weight <= -2 : 0x08 (level 3 only)
//   weight == -1 : 0x0c (levels 2, 3)
//   weight >=  0 : 0x0e (levels 1, 2, 3 — default)
inline constexpr uint8_t bwLevelsForInkWeight(const int8_t inkWeight) {
  if (inkWeight <= -2) return 0x08;
  if (inkWeight == -1) return 0x0c;
  return 0x0e;
}

// One BW blit pass: a logical offset (dx, dy) plus the 2-bit level mask to paint.
struct InkPass {
  int8_t dx;
  int8_t dy;
  uint8_t bwLevels;
};

struct InkPlan {
  InkPass passes[6];
  uint8_t count;
};

// Maps Paperback Look (0..2) and Ink Weight (-2..+2) into the BW blit sequence.
// Negative weights drop lower 2-bit fringes before any Paperback smear; positive
// weights add controlled horizontal then vertical/diagonal passes using the base
// mask so strokes darken monotonically even across anti-aliased fringes.
// TTF glyphs already baked inkWeight into their 2-bit coverage during packing, so
// they keep the default 0x0e mask and never double-filter here.
inline constexpr InkPlan inkPlanFor(const uint8_t paperbackLook, const int8_t inkWeight, const bool twoBit,
                                    const bool isTtf) {
  const int8_t w = (twoBit && !isTtf) ? (inkWeight < -2 ? -2 : (inkWeight > 2 ? 2 : inkWeight)) : 0;
  const uint8_t base = bwLevelsForInkWeight(w);
  InkPlan plan{{}, 0};
  plan.passes[plan.count++] = {0, 0, base};
  if (paperbackLook == 0) {
    if (w >= 1) plan.passes[plan.count++] = {1, 0, base};
    if (w >= 2) plan.passes[plan.count++] = {0, 1, base};
  } else if (paperbackLook == 1) {
    plan.passes[plan.count++] = {1, 0, base};
    plan.passes[plan.count++] = {0, 1, base};
    if (w >= 1) plan.passes[plan.count++] = {2, 0, base};
    if (w >= 2) plan.passes[plan.count++] = {1, 1, base};
  } else {
    plan.passes[plan.count++] = {1, 0, base};
    plan.passes[plan.count++] = {2, 0, base};
    plan.passes[plan.count++] = {0, 1, base};
    if (w >= 1) plan.passes[plan.count++] = {1, 1, base};
    if (w >= 2) plan.passes[plan.count++] = {2, 1, base};
  }
  return plan;
}

// Paint a packed glyph into target.
//
// bitmap: rows are contiguous, MSB first, 1 or 2 bits per pixel; widths need
//   not be byte-aligned. 2bpp values are 0=white, 1=light gray, 2=dark gray,
//   3=black. 1bpp value 1 is ink.
// plane: which 2bpp values are painted. BW paints the bits selected by bwLevels
//   (default 0x0e, every non-white value) and honours state (true clears the bit,
//   i.e. black). The gray planes only ever set bits, because there 0 means leave
//   alone and 1 means update: MSB paints both grays, LSB paints dark gray only.
//   1bpp glyphs paint ink with state on every plane.
// Clipping happens before pixel decoding, so fully hidden glyphs cost nothing.
inline void draw(const uint8_t* bitmap, int width, int height, bool twoBit, Plane plane, bool state,
                 const Target& target, Clip clip, uint8_t bwLevels = 0x0e) {
  // Bit n of levels set means source value n is painted.
  uint8_t levels = 0x02;
  bool clearBits = state;
  if (twoBit) {
    levels = plane == Plane::BW ? bwLevels : plane == Plane::GrayMSB ? 0x06 : 0x04;
    if (plane != Plane::BW) clearBits = false;
  }

  // Intersect with the glyph bounds, then with the panel width and the
  // buffered row band.
  clip.left = std::max(clip.left, 0);
  clip.top = std::max(clip.top, 0);
  clip.right = std::min(clip.right, width);
  clip.bottom = std::min(clip.bottom, height);
  clipToRect(target.frame, 0, target.originY, target.width, target.originY + target.rows, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  // Walk the framebuffer as a flat bit index. One glyph column advances
  // stepX bits and one glyph row advances stepY bits; each is +/-1 for the
  // axis that maps to physical x, or +/-strideBits for physical y.
  const Frame& frame = target.frame;
  const int strideBits = target.stride * 8;
  const int stepX = frame.dxY * strideBits + frame.dxX;
  const int stepY = frame.dyY * strideBits + frame.dyX;
  int rowBit = (frame.y - target.originY) * strideBits + frame.x + clip.left * stepX + clip.top * stepY;
  for (int y = clip.top; y < clip.bottom; ++y, rowBit += stepY) {
    int source = y * width + clip.left;
    int destination = rowBit;
    int remaining = clip.right - clip.left;
    if (twoBit) {
      // Rows are bit-contiguous, not byte-padded. Align after any clipped prefix.
      while (remaining && (source & 3)) {
        paint(target.buffer, destination, (bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3, levels, clearBits);
        ++source;
        destination += stepX;
        --remaining;
      }
      while (remaining >= 4) {
        const uint8_t packed = bitmap[source >> 2];
        paint(target.buffer, destination, packed >> 6, levels, clearBits);
        paint(target.buffer, destination + stepX, (packed >> 4) & 3, levels, clearBits);
        paint(target.buffer, destination + 2 * stepX, (packed >> 2) & 3, levels, clearBits);
        paint(target.buffer, destination + 3 * stepX, packed & 3, levels, clearBits);
        source += 4;
        destination += 4 * stepX;
        remaining -= 4;
      }
    }
    // One-bit glyphs and the clipped tail use scalar decoding.
    while (remaining--) {
      const uint8_t ink = twoBit ? ((bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3)
                                 : ((bitmap[source >> 3] >> (7 - (source & 7))) & 1);
      paint(target.buffer, destination, ink, levels, clearBits);
      ++source;
      destination += stepX;
    }
  }
}

}  // namespace glyphBitmap
