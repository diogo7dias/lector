#pragma once

#include <cstdint>

// WHICH pending anchor decides the page a freshly loaded chapter opens on.
//
// The reader can hold seven competing answers to "where should this land" at once: an
// explicit bookmark offset, a settings-reflow resume offset, a page jump, a fragment
// anchor, a percent jump, a paragraph ordinal, and a Sortes random page. Until this Module the precedence
// between them was implied by the ORDER of statements in render() and in
// applyDeferredReposition() — a nested ternary in one, a run of sequential if-blocks in
// the other, both interleaved with the SD reads that carry the decision out.
//
// Statement order is the worst possible place to keep a precedence rule: it cannot be
// read without reading every line between the branches, and it cannot be tested at all
// without a card, a book and a laid-out chapter. Every mistake it has produced looks the
// same from the outside — the reader lands on the wrong page after a font change, a
// bookmark, or a resume — and each fix so far has added another guard to the chain.
//
// So the decision is named here and the resolution stays where the data is: this Module
// says WHICH anchor wins, the caller does the page lookup that anchor implies. Pure: no
// Section, no Epub, no Storage. Same reason ReleaseGate and EdgeSchedule are pure.
namespace reader_landing {

// The anchors, in no particular order — precedence is the functions below, not this enum.
enum class Anchor : uint8_t {
  None,            // nothing pending: the saved page number stands as-is
  ExplicitOffset,  // a bookmark / sync jump to a stored content offset
  ResumeOffset,    // the settings-reflow anchor: same content, re-paginated
  PageJump,        // an explicit page number
  FragmentAnchor,  // a TOC / footnote href with a #fragment
  PercentJump,     // a book-percentage jump
  ParagraphScan,   // a cross-chapter Go To Paragraph
  OrdinalAnchor,   // the paragraph the reader was on when the layout changed
  PageFraction,    // last resort: rescale the old page number against the new page count
};

// What the reader currently has pending. Plain flags: the caller owns the optionals and
// the strings, and only says here whether each one is set.
struct Pending {
  bool explicitOffset = false;
  bool resumeOffset = false;  // cachedVisibleTextOffset
  bool pageJump = false;
  bool fragmentAnchor = false;
  bool percentJump = false;
  bool paragraphScan = false;
  bool ordinalAnchor = false;
  bool sortesPage = false;  // Sortes mode: open on a random page of the chapter
  // The reflow anchors name a page in the chapter they were captured in, so they mean
  // nothing once the reader has moved to a different one.
  bool sameSpineAsCapture = true;
  // A chapter total saved while the section was still building is a watermark, not the
  // real count; only a real one can rescale a page number.
  bool haveCapturedPageCount = false;
};

// Which anchor governs the content-offset landing of a section that has just loaded.
//
// An explicit bookmark jump always wins: it is a deliberate navigation to a stored
// content anchor. Otherwise the settings-change resume offset applies — but a page jump
// or a fragment anchor is a deliberate navigation that outranks it, and the resume
// offset names a position in the chapter it was captured in, so it does not survive a
// move to another spine item.
constexpr Anchor forOffsetLanding(const Pending& pending) {
  if (pending.explicitOffset) return Anchor::ExplicitOffset;
  if (pending.pageJump || pending.fragmentAnchor || !pending.sameSpineAsCapture) return Anchor::None;
  return pending.resumeOffset ? Anchor::ResumeOffset : Anchor::None;
}

// Which anchor decides the page once the chapter is laid out far enough to resolve it.
//
// The paragraph anchor outranks both fallbacks: it is the place the reader actually was,
// and a paragraph is a property of the book that does not move across a re-pagination.
// Below it, a content offset still names exact content; the page fraction is the last
// resort, and only when a real captured page count exists to rescale against.
constexpr Anchor forDeferredReposition(const Pending& pending) {
  if (!pending.sameSpineAsCapture) return Anchor::None;
  if (pending.ordinalAnchor) return Anchor::OrdinalAnchor;
  if (pending.resumeOffset) return Anchor::ResumeOffset;
  return pending.haveCapturedPageCount ? Anchor::PageFraction : Anchor::None;
}

// True when this anchor cannot resolve until the WHOLE chapter is laid out, so the
// caller must take the blocking build with the indexing popup rather than building
// incrementally to a page it cannot yet name.
//
// Only a percent jump and a Sortes page truly need it: both pick a page from the final
// page count (a fraction of it, or a random one below it). A
// fragment anchor resolves incrementally (the anchor is recorded as its page is laid
// out), and the settings-change reposition is deferred to forDeferredReposition() once
// the real page count is known, so neither blocks the first page.
constexpr bool needsFullBuild(const Pending& pending) { return pending.percentJump || pending.sortesPage; }

// True when the landing retires the deferred reposition outright. Only an explicit
// bookmark/sync/Return target does: it supersedes a stale session-start resume anchor.
// (An explicit offset that cannot be resolved is reported as a build error by the caller
// before this is consulted.)
//
// A resume offset that resolves also retires it, but that is decided at the resolution
// site — an unresolved resume anchor must survive for applyDeferredReposition() to retry
// once the background build has laid out more of the chapter.
constexpr bool retiresDeferredReposition(const Pending& pending) { return pending.explicitOffset; }

}  // namespace reader_landing
