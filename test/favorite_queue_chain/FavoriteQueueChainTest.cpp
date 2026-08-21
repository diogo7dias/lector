#include <gtest/gtest.h>

#include "util/FavoriteQueueChain.h"

using favorite_chain::Job;
using favorite_chain::resolve;

TEST(FavoriteQueueChain, EmptyQueueLeavesTheNameAlone) { EXPECT_EQ(resolve({}, "/sleep/a.pxc"), ""); }

TEST(FavoriteQueueChain, AQueuedRenameReportsItsTarget) {
  EXPECT_EQ(resolve({{"/sleep/a.pxc", "/sleep/a_F.pxc"}}, "/sleep/a.pxc"), "/sleep/a_F.pxc");
}

TEST(FavoriteQueueChain, AnUnrelatedQueuedRenameIsIgnored) {
  EXPECT_EQ(resolve({{"/sleep/b.pxc", "/sleep/b_F.pxc"}}, "/sleep/a.pxc"), "");
}

// The bug this file exists for. Favorite A, favorite B, unfavorite A: the queue holds
// A to A_F and later A_F back to A, with B's job between them so the cancel-on-toggle-back
// in request() cannot collapse them. Reading only the newest job whose source is A answers
// A_F, but A ends up exactly where it started.
TEST(FavoriteQueueChain, AChainThatReturnsToItsOriginReportsNoChange) {
  const std::vector<Job> queue = {
      {"/sleep/a.pxc", "/sleep/a_F.pxc"},
      {"/sleep/b.pxc", "/sleep/b_F.pxc"},
      {"/sleep/a_F.pxc", "/sleep/a.pxc"},
  };
  EXPECT_EQ(resolve(queue, "/sleep/a.pxc"), "");
  EXPECT_EQ(resolve(queue, "/sleep/b.pxc"), "/sleep/b_F.pxc");
}

TEST(FavoriteQueueChain, AChainFollowsThroughEveryHop) {
  const std::vector<Job> queue = {
      {"/sleep/a.pxc", "/sleep/a_F.pxc"},
      {"/sleep/a_F.pxc", "/sleep/a.pxc"},
      {"/sleep/a.pxc", "/sleep/a_F.pxc"},
  };
  EXPECT_EQ(resolve(queue, "/sleep/a.pxc"), "/sleep/a_F.pxc");
}

// Asking about a name that only appears part-way along a chain still answers correctly:
// the browser can hold the on-card name while the viewer holds the queued one.
TEST(FavoriteQueueChain, ResolvingFromTheMiddleOfAChain) {
  const std::vector<Job> queue = {
      {"/sleep/a.pxc", "/sleep/a_F.pxc"},
      {"/sleep/a_F.pxc", "/sleep/a.pxc"},
  };
  EXPECT_EQ(resolve(queue, "/sleep/a_F.pxc"), "/sleep/a.pxc");
}

TEST(FavoriteQueueChain, OrderMatters) {
  // The same two jobs in the other order describe a file that starts favorited.
  const std::vector<Job> queue = {
      {"/sleep/a_F.pxc", "/sleep/a.pxc"},
      {"/sleep/a.pxc", "/sleep/a_F.pxc"},
  };
  EXPECT_EQ(resolve(queue, "/sleep/a_F.pxc"), "");
}
