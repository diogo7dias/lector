#pragma once

// The value logic behind every slider screen: what a step does at the ends of
// the range, which physical button carries the large step, and what a drag
// lands on. Pure arithmetic, so the wrap rule and the X3 button flip are
// testable without a panel — both used to live twice, hand-written, in the
// percent picker and the interval picker.
namespace slider_field {

struct Range {
  int min = 0;
  int max = 100;
};

inline int clamp(const int value, const Range& range) {
  if (range.max <= range.min) return range.min;
  if (value < range.min) return range.min;
  if (value > range.max) return range.max;
  return value;
}

// Move by delta. Ordinary ranges clamp; a percent picker wraps instead, because
// a book has no end to run into — going up from 99% should reach the front
// cover, not stick. 0 and 100 are the same wrap point, so landing exactly on a
// multiple of 100 from below keeps 100 (90 + 10 = 100, not 0) and only a step
// that crosses it comes out the other side.
inline int step(const int value, const Range& range, const int delta, const bool wrap) {
  if (!wrap) return clamp(value + delta, range);
  const int span = range.max - range.min;
  if (span <= 0) return range.min;
  const int raw = value + delta;
  if (raw > range.min && (raw - range.min) % span == 0) return range.max;
  return range.min + (((raw - range.min) % span) + span) % span;
}

// Where a drag along the track lands. The SDK reports the touch as permille of
// the track's width, so the rounding is done here rather than in the paint.
inline int valueForPermille(const int permille, const Range& range) {
  if (range.max <= range.min) return range.min;
  const int bounded = permille < 0 ? 0 : (permille > 1000 ? 1000 : permille);
  const int span = range.max - range.min;
  return range.min + (bounded * span + 500) / 1000;
}

struct SideDeltas {
  int up = 0;
  int down = 0;
};

// On the X3 the side buttons sit on the left and right edges of the screen
// rather than as a vertical up/down rocker (X4), so BTN_UP is physically the
// left button and BTN_DOWN the right one. The large step flips there, so the
// left button always decreases and the right button always increases.
inline SideDeltas sideDeltas(const bool isX3, const int largeStep) {
  return isX3 ? SideDeltas{-largeStep, largeStep} : SideDeltas{largeStep, -largeStep};
}

}  // namespace slider_field
