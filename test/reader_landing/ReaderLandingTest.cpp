#include <gtest/gtest.h>

#include "activities/reader/ReaderLanding.h"

using namespace reader_landing;

namespace {

// A freshly loaded chapter in the spine the anchors were captured in, nothing pending.
Pending sameChapter() {
  Pending pending;
  pending.sameSpineAsCapture = true;
  return pending;
}

}  // namespace

// --- Which anchor governs the content-offset landing ---------------------------------

TEST(ReaderLanding, NothingPendingLeavesTheSavedPageAlone) { EXPECT_EQ(forOffsetLanding(sameChapter()), Anchor::None); }

// A bookmark or sync jump is a deliberate navigation to a stored content anchor.
TEST(ReaderLanding, AnExplicitOffsetOutranksEverything) {
  Pending pending = sameChapter();
  pending.explicitOffset = true;
  pending.resumeOffset = true;
  pending.pageJump = true;
  pending.fragmentAnchor = true;
  pending.sameSpineAsCapture = false;
  EXPECT_EQ(forOffsetLanding(pending), Anchor::ExplicitOffset);
}

TEST(ReaderLanding, TheResumeOffsetAppliesWhenNothingOutranksIt) {
  Pending pending = sameChapter();
  pending.resumeOffset = true;
  EXPECT_EQ(forOffsetLanding(pending), Anchor::ResumeOffset);
}

// A page jump or fragment anchor is a deliberate navigation; the reflow anchor is not.
TEST(ReaderLanding, DeliberateNavigationOutranksTheResumeOffset) {
  Pending pageJump = sameChapter();
  pageJump.resumeOffset = true;
  pageJump.pageJump = true;
  EXPECT_EQ(forOffsetLanding(pageJump), Anchor::None);

  Pending fragment = sameChapter();
  fragment.resumeOffset = true;
  fragment.fragmentAnchor = true;
  EXPECT_EQ(forOffsetLanding(fragment), Anchor::None);
}

// The reflow anchor names a position in the chapter it was captured in.
TEST(ReaderLanding, TheResumeOffsetDoesNotSurviveAChapterChange) {
  Pending pending = sameChapter();
  pending.resumeOffset = true;
  pending.sameSpineAsCapture = false;
  EXPECT_EQ(forOffsetLanding(pending), Anchor::None);
}

// --- Which anchor decides the deferred reposition ------------------------------------

TEST(ReaderLanding, TheParagraphAnchorOutranksBothFallbacks) {
  Pending pending = sameChapter();
  pending.ordinalAnchor = true;
  pending.resumeOffset = true;
  pending.haveCapturedPageCount = true;
  EXPECT_EQ(forDeferredReposition(pending), Anchor::OrdinalAnchor);
}

TEST(ReaderLanding, AContentOffsetOutranksThePageFraction) {
  Pending pending = sameChapter();
  pending.resumeOffset = true;
  pending.haveCapturedPageCount = true;
  EXPECT_EQ(forDeferredReposition(pending), Anchor::ResumeOffset);
}

TEST(ReaderLanding, ThePageFractionIsTheLastResort) {
  Pending pending = sameChapter();
  pending.haveCapturedPageCount = true;
  EXPECT_EQ(forDeferredReposition(pending), Anchor::PageFraction);
}

// A chapter total saved mid-build is a watermark, not the real count: with no real one
// there is nothing to rescale against.
TEST(ReaderLanding, WithoutACapturedPageCountThereIsNothingToRescale) {
  Pending pending = sameChapter();
  EXPECT_EQ(forDeferredReposition(pending), Anchor::None);
}

TEST(ReaderLanding, NoneOfTheDeferredAnchorsSurviveAChapterChange) {
  Pending pending;
  pending.sameSpineAsCapture = false;
  pending.ordinalAnchor = true;
  pending.resumeOffset = true;
  pending.haveCapturedPageCount = true;
  EXPECT_EQ(forDeferredReposition(pending), Anchor::None);
}

// --- Which landings must block on the whole chapter ----------------------------------

// Only percent -> page and a Sortes random page need the final page count.
TEST(ReaderLanding, OnlyAPercentJumpNeedsTheWholeChapterLaidOut) {
  Pending pending = sameChapter();
  EXPECT_FALSE(needsFullBuild(pending));

  pending.percentJump = true;
  EXPECT_TRUE(needsFullBuild(pending));
}

TEST(ReaderLanding, ASortesPageNeedsTheWholeChapterLaidOut) {
  Pending pending = sameChapter();
  pending.sortesPage = true;
  EXPECT_TRUE(needsFullBuild(pending));
}

// A fragment anchor resolves incrementally (recorded as its page is laid out), and the
// reflow reposition is deferred, so neither blocks the first page.
TEST(ReaderLanding, AnchorsAndReflowDoNotBlockTheFirstPage) {
  Pending pending = sameChapter();
  pending.fragmentAnchor = true;
  pending.resumeOffset = true;
  pending.ordinalAnchor = true;
  pending.haveCapturedPageCount = true;
  EXPECT_FALSE(needsFullBuild(pending));
}

// --- What retires the deferred reposition outright -----------------------------------

// An explicit target supersedes a stale session-start resume anchor.
TEST(ReaderLanding, AnExplicitJumpRetiresTheDeferredReposition) {
  Pending pending = sameChapter();
  pending.explicitOffset = true;
  pending.resumeOffset = true;
  EXPECT_TRUE(retiresDeferredReposition(pending));
}

// The regression this guards: an unresolved resume anchor must SURVIVE, so
// applyDeferredReposition() can retry once the background build lays out more pages.
// Retiring it here would strand the reader on the stale page.
TEST(ReaderLanding, AnUnresolvedResumeAnchorSurvivesToBeRetried) {
  Pending pending = sameChapter();
  pending.resumeOffset = true;
  EXPECT_FALSE(retiresDeferredReposition(pending));
}

TEST(ReaderLanding, NothingPendingRetiresNothing) { EXPECT_FALSE(retiresDeferredReposition(sameChapter())); }
