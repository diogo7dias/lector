#pragma once
#include <cstdint>

#include "PagePacker.h"

// Pure re-pack planning core for the "fast settings-change" path.
//
// When a settings change affects ONLY vertical layout (line spacing =
// lineCompression, top/bottom margin = viewportHeight, paragraphSpacing), the
// wrapped lines are unchanged; only how they pack into pages (page breaks + yPos)
// changes. This module re-packs a document-order stream of already-wrapped
// elements at NEW pack parameters, reusing the SAME pagepack::packElement the live
// build uses (so build and re-pack are identical by construction).
//
// The caller (Section::repackSectionFile, the file-I/O wiring) supplies one
// PackRecord per element -- read from the on-disk "pack sidecar" -- in the exact
// document order the elements were laid out, and receives, per element, the target
// page it lands on and its new yPos, plus per-page paragraph/list-item indices for
// the rebuilt LUTs. The element CONTENT (TextBlock/xPos/image) is copied verbatim
// from the cached source pages; only yPos + page assignment change here.
namespace repack {

enum ElemKind : uint8_t { ELEM_LINE = 0, ELEM_IMAGE = 1, ELEM_RULE = 2 };

// One record per laid-out element, in document order. The pack-parameter-DEPENDENT
// amounts (a text line's height, the extra-paragraph half-line, the paragraph-spacing
// gap) are stored as flags and recomputed at re-pack time from the target params; the
// pack-parameter-INDEPENDENT amounts (CSS block margins/padding, image/rule heights)
// are stored as fixed pixels.
struct PackRecord {
  uint8_t kind;             // ElemKind
  uint16_t pre;             // fixed px added before the element (block marginTop+paddingTop on a block's first line; else 0)
  uint16_t fixedHeight;     // element height for IMAGE/RULE; ignored for LINE (uses lineHeight)
  uint16_t post;            // fixed px added after (block marginBottom+paddingBottom on a block's last line; else 0)
  bool extraParaHalf;       // if set, add lineHeight/2 after this element (extraParagraphSpacing)
  bool paraSpacing;         // if set, add lineHeight*paragraphSpacing/100 after this element
  bool cutIfNotEmpty;       // rule/image: only break to a new page if the current page is non-empty (never orphan)
  uint16_t paragraphIndex;  // synthetic XPath p[N] active at this element (for the paragraph LUT)
  uint16_t listItemIndex;   // running list-item index active at this element (for the li LUT)
};

struct PackParams {
  int lineHeightPx;         // getLineHeight(fontId) * TARGET lineCompression
  int viewportHeight;       // TARGET viewport height (screen - margins - status band)
  int paragraphSpacingPct;  // TARGET paragraphSpacing (0..)
};

// Per-element result.
struct Placement {
  uint16_t page;  // 0-based target page this element lands on
  int16_t yPos;   // page-relative y position (matches PageElement::yPos)
};

// Compute the effective (pre, height, post) an element contributes at the target
// pack params. Split out so the file wiring and the tests share one definition.
inline void effectiveSpacing(const PackRecord& r, const PackParams& p, int& pre, int& height, int& post) {
  pre = r.pre;
  height = (r.kind == ELEM_LINE) ? p.lineHeightPx : r.fixedHeight;
  post = r.post;
  if (r.extraParaHalf) post += p.lineHeightPx / 2;
  if (r.paraSpacing) post += p.lineHeightPx * p.paragraphSpacingPct / 100;
}

// Streaming re-pack state: feed records in document order; it tracks the running
// page + y cursor exactly like the build's makePages/addLineToPage, delegating every
// page-cut decision to pagepack::packElement.
class Planner {
 public:
  explicit Planner(const PackParams& params) : params_(params) {}

  // Place the next element. Returns its {page, yPos}. `paragraphIndex`/`listItemIndex`
  // of the element are folded into the current page's metadata (last-writer per page,
  // matching the build which records the indices at page-complete time).
  Placement place(const PackRecord& r) {
    int pre, height, post;
    effectiveSpacing(r, params_, pre, height, post);
    const pagepack::PackDecision d =
        pagepack::packElement(currentNextY_, pageEmpty_, params_.viewportHeight, pre, height, post, r.cutIfNotEmpty);
    if (d.cutPage) {
      page_++;
    }
    currentNextY_ = d.nextY;
    pageEmpty_ = false;  // the page now holds this element
    placed_ = true;
    lastParagraphIndex_ = r.paragraphIndex;
    lastListItemIndex_ = r.listItemIndex;
    return Placement{page_, static_cast<int16_t>(d.yPos)};
  }

  uint16_t currentPage() const { return page_; }
  // Metadata for the page an element was just placed on (valid after place()).
  uint16_t pageParagraphIndex() const { return lastParagraphIndex_; }
  uint16_t pageListItemIndex() const { return lastListItemIndex_; }
  // Total pages produced == currentPage()+1 once at least one element was placed.
  uint16_t pageCount() const { return placed_ ? static_cast<uint16_t>(page_ + 1) : 0; }

 private:
  PackParams params_;
  int currentNextY_ = 0;
  uint16_t page_ = 0;
  bool pageEmpty_ = true;
  bool placed_ = false;  // set on first place()
  uint16_t lastParagraphIndex_ = 0;
  uint16_t lastListItemIndex_ = 0;
};

}  // namespace repack
