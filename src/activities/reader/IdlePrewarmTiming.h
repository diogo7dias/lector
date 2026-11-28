#pragma once

namespace idle_prewarm {

// How long after the last render the reader counts as idle: past rapid page-flipping, the
// background work (neighbour glyph prewarm, debounced progress save, chapter builds,
// next-chapter prefetch) may run.
inline constexpr unsigned long RENDER_DEBOUNCE_MS = 400;

// That work wants the full clock. Where the debounce would end after the idle clock drop
// (X4 Pro: 1000 ms after the last input, and a page turn alone takes 450 ms or more), the
// work starts this long before the drop instead, so a turn costs one clock drop and not
// two. Boards whose drop comes later (the C3 boards: 3000 ms) keep the plain debounce: it
// already ends long before the drop, and starting sooner would only put the work in the
// way of the next turn of someone flipping pages.
inline constexpr unsigned long CLOCK_DROP_MARGIN_MS = 350;

// True once a page has been rendered and either the debounce has passed, or the idle
// clock drop (clockDropMs after the last input) is closer than the margin. A render in
// flight is not seen here: the callers skip their work while the render lock is held.
constexpr bool isReady(const unsigned long nowMs, const unsigned long lastInputMs,
                       const unsigned long lastRenderCompleteMs, const unsigned long clockDropMs) {
  if (lastRenderCompleteMs == 0) return false;
  if (nowMs - lastRenderCompleteMs > RENDER_DEBOUNCE_MS) return true;
  return clockDropMs > CLOCK_DROP_MARGIN_MS && nowMs - lastInputMs >= clockDropMs - CLOCK_DROP_MARGIN_MS;
}

}  // namespace idle_prewarm
