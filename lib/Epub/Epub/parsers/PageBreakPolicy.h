#pragma once

#include <cstddef>
#include <cstdint>

namespace page_break_policy {

struct CarryDecision {
  size_t totalLines = 0;      // trailing lines to move onto the next page
  size_t paragraphLines = 0;  // portion belonging to the current paragraph
};

// Decides how many trailing lines move to the next page when a line overflows the viewport.
// - Disabled: pages always fill (0 lines carried).
// - Enabled:
//   * Never strand a heading at the bottom of a page: if no line of the following paragraph
//     stays on this page, carry the heading's trailing lines along with it.
//   * Avoid 1-line orphans (first line alone at the page bottom) and 1-line widows (last
//     line alone at the top of the next page) on paragraphs of 4+ lines. Shorter paragraphs
//     (2-3 lines) keep filling.
constexpr CarryDecision computeCarry(const bool enabled, const bool inParagraph, const bool hasDropCap,
                                     const uint8_t paragraphLinesOnPage, const uint16_t paragraphLinesEmitted,
                                     const int linesAfterCurrent, const uint8_t keepWithNextLines) {
  if (!enabled) return {};

  size_t paragraphLines = 0;
  if (inParagraph && !hasDropCap) {
    // linesAfterCurrent < 0 means a soft-flushed long paragraph whose remainder is not
    // laid out yet (always well over 4 lines).
    const bool isFourPlus =
        linesAfterCurrent < 0 || (static_cast<int>(paragraphLinesEmitted) + 1 + linesAfterCurrent >= 4);
    if (isFourPlus) {
      if (paragraphLinesOnPage == 1 && paragraphLinesEmitted == 1) {
        paragraphLines = 1;  // 1-line orphan at page bottom
      } else if (linesAfterCurrent == 0 &&
                 (paragraphLinesEmitted == paragraphLinesOnPage ? paragraphLinesOnPage >= 3
                                                                : paragraphLinesOnPage >= 2)) {
        paragraphLines = 1;  // 1-line widow at top of next page
      }
    }
  }

  size_t total = paragraphLines;
  if (keepWithNextLines > 0 && paragraphLines == paragraphLinesOnPage) {
    total += keepWithNextLines;
  }
  return {total, paragraphLines};
}

}  // namespace page_break_policy
