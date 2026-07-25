#pragma once

#include <cstdint>

class GfxRenderer;

// A small strip across the top of the screen with short, non-bold text, shown
// while something slow is happening. One shared look for every wait in the
// firmware, in place of each screen inventing its own.
//
// It is deliberately late, not eager. There is no regional e-ink refresh wired
// up (HalDisplay only ever refreshes the whole panel), so painting the banner
// costs a real refresh. Showing it before every operation would make the quick
// ones measurably slower for no benefit. Instead the banner arms itself, the
// operation calls busy::tick() from inside its existing loop, and the strip
// appears only once the work has genuinely been running longer than the delay.
// Work that is always slow can skip the wait with showNow().
//
// Usage:
//   BusyBanner banner(renderer, tr(STR_INDEXING));   // armed, nothing drawn
//   ... slow loop calling busy::tick() ...           // appears if it drags
// The banner does not erase itself: every caller renders a full screen after the
// work finishes, which overwrites it.
class BusyBanner {
 public:
  // How long an operation must run before the banner is worth a panel refresh.
  static constexpr uint32_t DEFAULT_DELAY_MS = 400;

  BusyBanner(GfxRenderer& renderer, const char* text, uint32_t delayMs = DEFAULT_DELAY_MS);
  ~BusyBanner();

  BusyBanner(const BusyBanner&) = delete;
  BusyBanner& operator=(const BusyBanner&) = delete;

  // Draws the banner right away, for work known to be slow every time and with
  // no loop to tick from. Idempotent.
  void showNow();

  bool shown() const { return drawn; }

 private:
  void onTick();
  static void tickTrampoline();
  static void tickNowTrampoline();

  GfxRenderer& renderer;
  const char* text;
  uint32_t delayMs;
  uint32_t startMs;
  // Banners nest: opening a book loads a font and then builds an index, each
  // wanting its own label. The inner one restores this on the way out.
  BusyBanner* previous = nullptr;
  bool drawn = false;
};
