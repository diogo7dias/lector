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

  // Promote the next refresh — whatever it asks for — to a FULL, once.
  //
  // A cold boot inherits whatever charge the panel was already carrying, and the first
  // paint is normally a differential FAST, which drives only the pixels that changed. Every
  // unchanged pixel keeps the old image, so the previous session shows through the new
  // screen as speckle and stale text. Boot paths that deliberately preserve the panel (a
  // quick resume over a retained frame, a wallpaper wake that already runs its own FULL)
  // do not set this; every other boot does, and pays one flash to start from a clean panel.
  void forceNextFull() { forceFull_ = true; }

 private:
  uint8_t consecutiveFast_ = 0;
  uint8_t fastSinceFull_ = 0;
  bool forceFull_ = false;
};
