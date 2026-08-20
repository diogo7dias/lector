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

  // Ink debt: what the counters above cannot see.
  //
  // The two counters treat every FAST pass as equal. They are not equal. A page of body
  // text moves a little ink in a lot of places; a black dialog dropped over a cover
  // inverts half the panel. Counting both as "one pass" is why a reading session stays
  // clean while a session spent in menus and covers ghosts, on identical counts.
  //
  // So each FAST pass is also charged what it actually costs the panel, on
  // FrameInkMetrics' 0..1000 scale, and a clean is forced once the running total crosses
  // a threshold. The thresholds are set from measured behaviour: an ordinary text page
  // turn scores about 300, so it reaches DEBT_CLEAN_THRESHOLD after roughly twelve turns
  // — the same cadence MAX_CONSECUTIVE_FAST already gave it, i.e. reading is unchanged.
  // A whole-frame inversion scores 1000 and gets there in four.
  //
  // This can only ever escalate EARLIER than the counters would have. The counters stay
  // as a hard ceiling, so no content can be scored low enough to delay a clean past where
  // it happens today, and every behaviour the existing tests pin still holds.
  static constexpr uint16_t DEBT_CLEAN_THRESHOLD = 3800;

  // Far above the clean threshold, and a backstop rather than the usual route to a FULL:
  // MAX_FAST_BEFORE_FULL is what a reading session normally hits. This is for a session
  // that is nothing but heavy full-screen swaps, where forty-eight passes is too long to
  // wait for a discharge.
  static constexpr uint16_t DEBT_FULL_THRESHOLD = 12000;

  // What a Clean pass actually removes. Not everything: a HALF scrub drives every pixel
  // to its target but does not discharge the panel — that difference is the whole reason
  // MAX_FAST_BEFORE_FULL exists. Leaving a fifth of the debt behind means a screen that
  // keeps earning cleans gradually earns them sooner, and eventually earns a FULL.
  static constexpr uint16_t CLEAN_DISCHARGE_DIVISOR = 5;

  // nowMs is accepted for call-site stability but no longer consulted: idle time
  // on an e-reader is the user reading the page, so it must never trigger a
  // clean. Ghosting cleanup is driven by MAX_CONSECUTIVE_FAST instead.
  //
  // inkScore is this frame's cost from FrameInkMetrics. It defaults to 0, which is the
  // "no metrics available" case and reproduces the counter-only behaviour exactly.
  Mode choose(Mode requested, uint32_t nowMs, uint16_t inkScore = 0);
  void reset();

  // Ink debt outstanding, carried across a lock alongside fastSinceFull(). Same reason:
  // waking is a chip reset, and a reader who locks often would otherwise start every
  // session with a clean slate the panel does not share.
  uint16_t inkDebt() const { return inkDebt_; }
  void seedInkDebt(uint16_t value) { inkDebt_ = value > DEBT_FULL_THRESHOLD ? DEBT_FULL_THRESHOLD : value; }

  // FAST passes since the last full discharge. Deep sleep is a chip reset, so this
  // budget dies with every lock unless the caller carries it across: a reader who
  // sleeps the device every 20 pages would otherwise never reach MAX_FAST_BEFORE_FULL
  // and never get the one pass that actually discharges the panel.
  uint8_t fastSinceFull() const { return fastSinceFull_; }
  void seedFastSinceFull(uint8_t value) {
    fastSinceFull_ = value > MAX_FAST_BEFORE_FULL ? MAX_FAST_BEFORE_FULL : value;
  }

  // Charged to a pass the policy never got to choose. Half of a whole-frame inversion:
  // heavier than a page of text, lighter than driving every pixel from black to white.
  static constexpr uint16_t EXTERNAL_PASS_SCORE = 500;

  // A pass that drove the panel without going through choose(): the grayscale planes,
  // which are pushed straight to the driver. They leave charge like any other pass, so
  // they spend the same budget; saturates rather than wrapping. The ink charge defaults
  // to EXTERNAL_PASS_SCORE rather than to zero because these passes drive the panel with
  // one-frame phases and leave more residue than an ordinary differential — and none of
  // it can be measured from the black-and-white framebuffer.
  void noteExternalFastPass(uint16_t inkScore = EXTERNAL_PASS_SCORE) {
    if (consecutiveFast_ < 0xFF) ++consecutiveFast_;
    if (fastSinceFull_ < MAX_FAST_BEFORE_FULL) ++fastSinceFull_;
    addDebt(inkScore);
  }

 private:
  // Saturating, so a long session cannot wrap the total back to "clean".
  void addDebt(uint32_t amount) {
    const uint32_t total = inkDebt_ + amount;
    inkDebt_ = total > DEBT_FULL_THRESHOLD ? DEBT_FULL_THRESHOLD : static_cast<uint16_t>(total);
  }

  uint8_t consecutiveFast_ = 0;
  uint8_t fastSinceFull_ = 0;
  uint16_t inkDebt_ = 0;
};
