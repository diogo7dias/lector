#pragma once

#include <cstdint>

class DisplayRefreshPolicy {
 public:
  enum class Mode : uint8_t { Fast, Clean, Full };

  static constexpr uint8_t MAX_CONSECUTIVE_FAST = 12;

  // A Clean (HALF) pass tidies the panel but never fully discharges it, so a long
  // run of FAST passes broken up only by HALFs still accumulates ghosting — the
  // OPDS download path alone fires 20+ full-screen FAST paints. Force a real FULL
  // after this many FAST passes since the last FULL. Deliberately much larger than
  // MAX_CONSECUTIVE_FAST so reading pays one slow flash every ~48 page turns.
  static constexpr uint8_t MAX_FAST_BEFORE_FULL = 48;

  // nowMs is accepted for call-site stability but no longer consulted: idle time
  // on an e-reader is the user reading the page, so it must never trigger a
  // clean. Ghosting cleanup is driven by MAX_CONSECUTIVE_FAST instead.
  Mode choose(Mode requested, uint32_t nowMs);
  void reset();

  // FAST passes since the last full discharge. Deep sleep is a chip reset, so this
  // budget dies with every lock unless the caller carries it across: a reader who
  // sleeps the device every 20 pages would otherwise never reach MAX_FAST_BEFORE_FULL
  // and never get the one pass that actually discharges the panel.
  uint8_t fastSinceFull() const { return fastSinceFull_; }
  void seedFastSinceFull(uint8_t value) {
    fastSinceFull_ = value > MAX_FAST_BEFORE_FULL ? MAX_FAST_BEFORE_FULL : value;
  }

  // A pass that drove the panel without going through choose(): the grayscale planes,
  // which are pushed straight to the driver. They leave charge like any other pass, so
  // they spend the same budget; saturates rather than wrapping.
  void noteExternalFastPass() {
    if (consecutiveFast_ < 0xFF) ++consecutiveFast_;
    if (fastSinceFull_ < MAX_FAST_BEFORE_FULL) ++fastSinceFull_;
  }

 private:
  uint8_t consecutiveFast_ = 0;
  uint8_t fastSinceFull_ = 0;
};
