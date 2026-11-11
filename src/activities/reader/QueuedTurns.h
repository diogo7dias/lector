#pragma once

#include <algorithm>
#include <cstdint>

// Page-turn presses that arrive while the panel is refreshing are counted, up to four either
// way, and applied together once it is free, so the reader paints only the page it lands on
// (upstream #4428). An opposite press cancels one.
constexpr int8_t kMaxQueuedTurns = 4;

constexpr int8_t queueTurn(const int8_t pending, const bool forward) {
  return static_cast<int8_t>(
      std::clamp(pending + (forward ? 1 : -1), static_cast<int>(-kMaxQueuedTurns), static_cast<int>(kMaxQueuedTurns)));
}

// How many of `queued` turns one pass may apply: those that stay on this chapter's laid-out
// pages, plus one that steps past them, as a single press does (onto a page still being built,
// or into the next or previous chapter). The rest are dropped: past that step the chapter has
// to load or lay out before there is a page to land on.
// ponytail: dropped, not carried into the next chapter; carry them if readers miss them.
constexpr int queuedTurnsNow(const int queued, const bool forward, const int currentPage, const int pageCount) {
  return std::min(queued, std::max(1, forward ? pageCount - currentPage : currentPage + 1));
}
