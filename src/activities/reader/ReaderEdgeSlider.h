#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Live frontlight edge sliders for touch readers: a vertical drag inside the left 12%
// strip adjusts brightness, and inside the right 12% strip adjusts warmth. Pure integer
// state machine so it can be host-tested without hardware or activity state.
class ReaderEdgeSlider {
 public:
  enum class Axis : uint8_t { None, Brightness, Warmth };

  struct Step {
    bool active = false;  // true once dragging: caller absorbs touch for this pass
    bool valueChanged = false;
    Axis axis = Axis::None;
    uint8_t value = 0;
    bool turnOn = false;  // brightness moved above 0 while light was off
  };

  static constexpr int EDGE_STRIP_PERCENT = 12;
  static constexpr int VERTICAL_MARGIN_PERCENT = 10;
  static constexpr int DRAG_THRESHOLD_PX = 24;
  static constexpr int HORIZ_ABORT_PX = 24;
  static constexpr int SPAN_PERCENT = 60;

  bool isDragging() const { return phase_ == Phase::Dragging; }

  Step update(const int x, const int y, const int width, const int height, const bool hasWarmth, const bool lightOn,
              const uint8_t curBrightness, const uint8_t maxBrightness, const uint8_t curWarmth) {
    Step out;
    if (width <= 0 || height <= 0) return out;

    if (phase_ == Phase::Idle) {
      const int marginY = (height * VERTICAL_MARGIN_PERCENT) / 100;
      if (y < marginY || y >= height - marginY) {
        phase_ = Phase::Rejected;
        return out;
      }
      const int stripW = (width * EDGE_STRIP_PERCENT) / 100;
      if (x < stripW) {
        axis_ = Axis::Brightness;
        startValue_ = lightOn ? curBrightness : 0;
        maxValue_ = std::max<uint8_t>(1, maxBrightness);
      } else if (hasWarmth && x >= width - stripW) {
        axis_ = Axis::Warmth;
        startValue_ = curWarmth;
        maxValue_ = 100;
      } else {
        phase_ = Phase::Rejected;
        return out;
      }
      phase_ = Phase::Armed;
      startX_ = x;
      startY_ = y;
      lastValue_ = startValue_;
      lightOn_ = lightOn;
      dirty_ = false;
      return out;
    }

    if (phase_ == Phase::Rejected) return out;

    if (phase_ == Phase::Armed) {
      const int dx = std::abs(x - startX_);
      const int dy = std::abs(y - startY_);
      if (dx >= HORIZ_ABORT_PX && dx > dy) {
        phase_ = Phase::Rejected;
        return out;
      }
      if (dy < DRAG_THRESHOLD_PX || dy < dx * 2) return out;
      phase_ = Phase::Dragging;
      // Anchor drag from the arming point so crossing the threshold does not jump the level.
      startY_ = y;
    }

    out.active = true;
    out.axis = axis_;
    const int span = std::max(1, (height * SPAN_PERCENT) / 100);
    const int delta = ((startY_ - y) * static_cast<int>(maxValue_)) / span;
    const auto next =
        static_cast<uint8_t>(std::clamp(static_cast<int>(startValue_) + delta, 0, static_cast<int>(maxValue_)));
    out.value = next;
    if (next != lastValue_) {
      lastValue_ = next;
      dirty_ = true;
      out.valueChanged = true;
      if (axis_ == Axis::Brightness && next > 0 && !lightOn_) {
        lightOn_ = true;
        out.turnOn = true;
      }
    }
    return out;
  }

  // Call when the finger lifts (or when another gesture / activity exit interrupts the drag).
  // Returns true iff a drag actually changed a setting so the caller saves once on lift.
  bool finish() {
    const bool save = (phase_ == Phase::Dragging) && dirty_;
    phase_ = Phase::Idle;
    axis_ = Axis::None;
    dirty_ = false;
    return save;
  }

 private:
  enum class Phase : uint8_t { Idle, Armed, Dragging, Rejected };

  Phase phase_ = Phase::Idle;
  Axis axis_ = Axis::None;
  int startX_ = 0;
  int startY_ = 0;
  uint8_t startValue_ = 0;
  uint8_t lastValue_ = 0;
  uint8_t maxValue_ = 100;
  bool lightOn_ = false;
  bool dirty_ = false;
};
