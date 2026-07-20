#pragma once

#include <cstdint>

// PagePacker — the pure vertical page-PACKING arithmetic, extracted from
// ChapterHtmlSlimParser so that (a) the live layout build and (b) the future
// "re-pack without re-wrap" fast path share ONE implementation and cannot
// diverge, and (c) it can be host-unit-tested with no ESP/renderer deps.
//
// The packer never touches pages, files, or the renderer. It answers one
// question per element: given the current vertical cursor and whether the
// current page already holds anything, does this element start a new page,
// what y-position does it get, and where is the cursor afterwards?
//
// WRAP (which words land on which line) is NOT here — that is the expensive,
// non-portable step in ParsedText/ChapterHtmlSlimParser. PACK is pure integer
// geometry, identical for a fresh build and a re-pack at the same settings.

namespace pagepack {

// Result of placing one element (a text line, an image, or a horizontal rule).
struct PackDecision {
  bool cutPage;    // caller must flush the current page and start a fresh one first
  int yPos;        // y-position to place the element at (on the possibly-new page)
  int nextY;       // vertical cursor after the element (feed back as currentNextY)
};

// Decide placement of one element of total footprint pre+height+post.
//   currentNextY  : the running vertical cursor on the current page
//   pageEmpty     : true if the current page has no elements yet
//   viewportHeight: usable page height in px
//   pre / post    : spacing reserved before / after the element (px)
//   height        : the element's own height (px)
//   cutIfNotEmpty : true  -> only break to a new page when the page is non-empty
//                            (images / rules never orphan onto an empty page)
//                   false -> break whenever it does not fit (text lines)
//
// Mirrors the original inline logic exactly: the fit test uses the element's
// FULL footprint (pre+height+post), a break resets the cursor to 0, then the
// cursor advances by pre (giving yPos), then by height+post.
inline PackDecision packElement(int currentNextY, bool pageEmpty, int viewportHeight, int pre, int height, int post,
                                bool cutIfNotEmpty) {
  const bool wouldOverflow = currentNextY + pre + height + post > viewportHeight;
  const bool cut = (cutIfNotEmpty ? !pageEmpty : true) && wouldOverflow;
  const int startY = cut ? 0 : currentNextY;
  const int yPos = startY + pre;
  const int nextY = yPos + height + post;
  return PackDecision{cut, yPos, nextY};
}

// Pure vertical advance with NO page break — block margins/padding and the
// inter-paragraph gaps just push the cursor down; the NEXT element's fit test
// accounts for the overflow (matches the original makePages behavior).
inline int advanceY(int currentNextY, int delta) { return currentNextY + delta; }

}  // namespace pagepack
