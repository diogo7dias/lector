#pragma once

// Where the four front-button hints sit, and which one a tap landed on. Pure geometry so
// the device side stays a draw loop and the answer can be tested on the host.
//
// Two shapes, because the band means two different things. On a button-only board it is a
// legend: four 106 px boxes under the four physical keys, positioned to line up with them.
// On a touch board it is also the control itself, so the boxes give way to four columns
// that tile the full width — no dead glass between them to swallow a press.
namespace hint_band {

struct Band {
  int screenWidth;
  int screenHeight;
  int bandHeight;  // metrics.buttonHintsHeight, already touch-adjusted by the caller
  bool touch;
  bool isX3;
};

struct Slot {
  int x;
  int y;
  int width;
  int height;
};

constexpr int kSlotCount = 4;
constexpr int kButtonBoxWidth = 106;
// Positions line the boxes up with the physical keys. The X3's portrait screen is wider
// (528 vs 480), so its keys sit further apart.
constexpr int kX4BoxX[kSlotCount] = {25, 130, 245, 350};
constexpr int kX3BoxX[kSlotCount] = {38, 154, 268, 384};

inline Slot slot(const Band& band, const int index) {
  const int y = band.screenHeight - band.bandHeight;
  if (!band.touch) {
    const int* positions = band.isX3 ? kX3BoxX : kX4BoxX;
    return Slot{positions[index], y, kButtonBoxWidth, band.bandHeight};
  }
  // Integer division leaves up to three pixels over; the last column takes them so the
  // row ends exactly at the screen edge.
  const int width = band.screenWidth / kSlotCount;
  const int x = index * width;
  const int last = index == kSlotCount - 1;
  return Slot{x, y, last ? band.screenWidth - x : width, band.bandHeight};
}

// The slot a point falls in, or -1. Callers still have to check that the slot they are
// given carries a label: an empty hint draws no box and must answer to no press.
inline int fromPoint(const Band& band, const int x, const int y) {
  for (int i = 0; i < kSlotCount; ++i) {
    const Slot s = slot(band, i);
    if (x >= s.x && x < s.x + s.width && y >= s.y && y < s.y + s.height) return i;
  }
  return -1;
}

// The slot a tap acts on: inside the band, and carrying a label this frame. A hint with no
// label draws no box, so pressing where it would have been must do nothing. Only a touch
// board answers at all — on a button board the band is a legend, not a control.
inline int tappedSlot(const Band& band, const int x, const int y, const bool labelled[kSlotCount]) {
  if (!band.touch) return -1;
  const int index = fromPoint(band, x, y);
  if (index < 0 || !labelled[index]) return -1;
  return index;
}

// What drawButtonHints() last painted. The band is drawn by the theme and tapped through
// MappedInputManager, which never sees the labels, so the two meet here: one frame's worth
// of "these slots exist and are pressable". Overwritten on every paint.
struct Painted {
  Band band{0, 0, 0, false, false};
  bool labelled[kSlotCount] = {false, false, false, false};
  bool valid = false;
};

inline Painted& lastPainted() {
  static Painted painted;
  return painted;
}

}  // namespace hint_band
