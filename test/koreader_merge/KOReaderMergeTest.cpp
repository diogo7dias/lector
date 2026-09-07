#include <SyncDecision.h>
#include <gtest/gtest.h>

#include <limits>

namespace {

using kosync::decideMerge;
using kosync::MergeChoice;

TEST(KOReaderMerge, FurthestPositionWins) {
  EXPECT_EQ(decideMerge(0.60f, 0.40f), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.40f, 0.60f), MergeChoice::APPLY_REMOTE);
}

TEST(KOReaderMerge, EqualWithinEpsilonIsAlreadySynced) {
  EXPECT_EQ(decideMerge(0.5f, 0.5f), MergeChoice::ALREADY_SYNCED);
  EXPECT_EQ(decideMerge(0.5f, 0.5f + kosync::SAME_PROGRESS_EPSILON / 2), MergeChoice::ALREADY_SYNCED);
  EXPECT_EQ(decideMerge(0.5f, 0.5f - kosync::SAME_PROGRESS_EPSILON / 2), MergeChoice::ALREADY_SYNCED);
}

TEST(KOReaderMerge, JustOutsideEpsilonPicksASide) {
  EXPECT_EQ(decideMerge(0.5f, 0.5f - 0.01f), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.5f, 0.5f + 0.01f), MergeChoice::APPLY_REMOTE);
}

// The point of the rule: a server record that cannot be a position never
// overwrites the reader's own position.
TEST(KOReaderMerge, UnusableRemoteNeverOverwritesLocal) {
  EXPECT_EQ(decideMerge(0.5f, std::numeric_limits<float>::quiet_NaN()), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.5f, std::numeric_limits<float>::infinity()), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.5f, -0.2f), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.5f, 1.5f), MergeChoice::UPLOAD_LOCAL);
  // Even at the start of the book, where a wrong apply costs the least.
  EXPECT_EQ(decideMerge(0.0f, std::numeric_limits<float>::quiet_NaN()), MergeChoice::UPLOAD_LOCAL);
}

TEST(KOReaderMerge, UnusableLocalTakesTheRemote) {
  EXPECT_EQ(decideMerge(std::numeric_limits<float>::quiet_NaN(), 0.3f), MergeChoice::APPLY_REMOTE);
  EXPECT_EQ(decideMerge(-1.0f, 0.3f), MergeChoice::APPLY_REMOTE);
}

TEST(KOReaderMerge, BookEndsAreOrdinaryValues) {
  EXPECT_EQ(decideMerge(1.0f, 0.0f), MergeChoice::UPLOAD_LOCAL);
  EXPECT_EQ(decideMerge(0.0f, 1.0f), MergeChoice::APPLY_REMOTE);
  EXPECT_EQ(decideMerge(1.0f, 1.0f), MergeChoice::ALREADY_SYNCED);
}

}  // namespace
