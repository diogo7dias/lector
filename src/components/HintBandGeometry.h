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

// The stroke a band tap stands for. A tap is one event, but a button is two: the press
// is answered on the frame the tap lands and the release on the next, because that is
// what a physical key does and what every hold-aware button on top of one expects.
// Delivering both in a single frame let whichever query the caller wrote first eat the
// tap, and a screen reading press and release together (the file browser's Open, which
// also holds) saw half a stroke and did nothing. Pure state, so the order can be tested
// on the host.
struct TapStroke {
  // A screen that never asks for the release must not leave one lying about for the
  // next thing that does, so it expires. Allow a full e-ink refresh (1–2 s) plus
  // rendering/scheduling margin before the next input frame can claim it.
  static constexpr unsigned long kReleaseWindowMs = 5000;

  // The tap frame's single press is spent: asking again for the same tap gets nothing.
  bool used = false;
  // The hardware id whose synthetic release is still owed, or -1, and when the press
  // that owes it landed.
  int pendingRelease = -1;
  unsigned long pendingAt = 0;

  // One press or release query for hardware `hw`, on a frame whose band tap (if any)
  // stands for `tapped` (-1 when no tap is live). Returns whether the query is answered.
  bool query(const int hw, const int tapped, const bool releaseQuery, const unsigned long now) {
    if (tapped < 0) {
      // The synthetic release, owed from the frame the tap landed on. Delivered only
      // once the tap event itself is gone, so press and release never share a frame.
      if (!releaseQuery || pendingRelease != hw) return false;
      pendingRelease = -1;
      return now - pendingAt <= kReleaseWindowMs;
    }
    if (used || hw != tapped) return false;
    // A tap always owes a release, including when this is the first query a
    // release-only screen makes. The tap frame itself stays press-only.
    used = true;
    pendingRelease = hw;
    pendingAt = now;
    return !releaseQuery;
  }

  // The tap event is over; the next one starts unspent.
  void tapOver() { used = false; }

  // Drops a release not yet delivered. Called on every screen change: the release
  // belongs to the screen that handled the press, and an orphan one would act on
  // whatever the new screen has selected.
  void clear() {
    used = false;
    pendingRelease = -1;
  }
};

}  // namespace hint_band
